// LyricView.mm
// foo_openlyrics_mac —— Plan 2 Task 5：LyricView 实现。
//
// 渲染策略：不用逐行 NSTextField 子视图（子视图数量随歌词行数变化，频繁增删开销大且约束
// 管理麻烦），改用单个 NSView 的 -drawRect: 手绘。每行的 NSAttributedString（普通态 + 高亮态
// 各一份）只在 -setLyricData: 换歌词时重建一次并缓存进 _normalAttrLines/_highlightAttrLines；
// 之后每次 -setSyncResult:（外部 tick 推来）或内部 ~60ms 缓动 tick，都只重新计算滚动偏移量与
// 当前高亮行索引，drawRect: 直接查表取已缓存的富文本绘制，不重排版、不重新分配。
//
// 坐标系：isFlipped=YES，原点在左上角、y 向下增长，这样"第 i 行的内容顶部 y 坐标 = i*行高"
// 可以直接对应到 view 坐标，不必再做一次翻转换算。
#import "LyricView.h"
#import "stdafx.h"

#include <algorithm>
#include <cmath>

static NSString *const kPlaceholderNoLyrics = @"无歌词";

// 逐帧向目标滚动偏移缓动的步进比例：每 tick 消化 (target-current) 的这个比例，
// 数值越大跟手越快、越小越"粘滞"。0.22 在 60ms 步进下大约 200~300ms 内追上目标，
// 观感是平滑跟随而不是瞬间跳变。
static const double kScrollEasing = 0.22;
// 判定"已追上目标、可以停止继续插值"的像素阈值，避免浮点尾数导致的无限小步进。
static const double kScrollSnapThreshold = 0.25;
// 单帧滚动动画的 tick 间隔。
static const NSTimeInterval kAnimTickInterval = 0.06;

static NSFont *NormalFont(void) {
    static NSFont *font = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ font = [NSFont systemFontOfSize:15]; });
    return font;
}

static NSFont *HighlightFont(void) {
    static NSFont *font = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ font = [NSFont boldSystemFontOfSize:16]; });
    return font;
}

static NSColor *NormalTextColor(void) {
    static NSColor *color = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ color = [NSColor colorWithCalibratedWhite:0.62 alpha:1.0]; });
    return color;
}

static NSColor *HighlightTextColor(void) {
    static NSColor *color = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ color = [NSColor colorWithCalibratedRed:1.0 green:0.84 blue:0.35 alpha:1.0]; });
    return color;
}

static NSColor *PlaceholderTextColor(void) {
    static NSColor *color = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ color = [NSColor colorWithCalibratedWhite:0.45 alpha:1.0]; });
    return color;
}

static NSColor *BackgroundColor(void) {
    static NSColor *color = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ color = [NSColor colorWithCalibratedWhite:0.11 alpha:1.0]; });
    return color;
}

@implementation LyricView {
    openlyrics::LyricData _data;
    openlyrics::SyncResult _syncResult;
    NSArray<NSAttributedString *> *_normalAttrLines;
    NSArray<NSAttributedString *> *_highlightAttrLines;
    NSAttributedString *_placeholderAttr;

    double _scrollOffset;        // 当前已渲染（插值中）的滚动偏移，像素
    double _targetScrollOffset;  // 依据最新 SyncResult 算出的目标滚动偏移，像素
    CGFloat _lineHeight;

    NSTimer *_animTimer;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self != nil) {
        _lineHeight = 30.0;
        _normalAttrLines = @[];
        _highlightAttrLines = @[];
        _placeholderAttr = [[NSAttributedString alloc] initWithString:kPlaceholderNoLyrics
                                                             attributes:@{
                                                                 NSFontAttributeName : NormalFont(),
                                                                 NSForegroundColorAttributeName : PlaceholderTextColor(),
                                                             }];
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)dealloc {
    [_animTimer invalidate];
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    if (self.window == nil) {
        [self stopAnimationTimer];
    } else if (_data.synced && _normalAttrLines.count > 0) {
        [self startAnimationTimerIfNeeded];
    }
}

#pragma mark - 公开接口

- (void)setLyricData:(const openlyrics::LyricData &)data {
    _data = data;
    _syncResult = openlyrics::SyncResult{};
    _scrollOffset = 0;
    _targetScrollOffset = 0;

    [self rebuildCachedLines];

    if (_data.synced && _normalAttrLines.count > 0) {
        [self startAnimationTimerIfNeeded];
    } else {
        [self stopAnimationTimer];
    }

    self.needsDisplay = YES;
}

- (void)setSyncResult:(const openlyrics::SyncResult &)result {
    if (!_data.synced || _normalAttrLines.count == 0) return;  // 未同步/无歌词时不响应播放位置

    _syncResult = result;
    _targetScrollOffset = [self computeTargetScrollOffsetForResult:result];

    // 目标与当前渲染值相差超过一屏（比如切歌瞬间、大幅 seek），直接跳过缓动一步到位，
    // 避免出现"追不上"的长时间违和滚动；正常播放推进时差值很小，交给 tickAnimation 缓动。
    if (std::fabs(_targetScrollOffset - _scrollOffset) > self.bounds.size.height) {
        _scrollOffset = _targetScrollOffset;
    }
    self.needsDisplay = YES;
}

#pragma mark - 内部：数据准备

- (void)rebuildCachedLines {
    const size_t count = _data.lines.size();
    NSMutableArray<NSAttributedString *> *normal = [NSMutableArray arrayWithCapacity:count];
    NSMutableArray<NSAttributedString *> *highlight = [NSMutableArray arrayWithCapacity:count];

    NSDictionary *normalAttrs = @{
        NSFontAttributeName : NormalFont(),
        NSForegroundColorAttributeName : NormalTextColor(),
    };
    NSDictionary *highlightAttrs = @{
        NSFontAttributeName : HighlightFont(),
        NSForegroundColorAttributeName : HighlightTextColor(),
    };

    for (const auto &line : _data.lines) {
        NSString *text = @"";
        if (!line.text.empty()) {
            NSString *converted = [NSString stringWithUTF8String:line.text.c_str()];
            if (converted != nil) text = converted;
        }
        [normal addObject:[[NSAttributedString alloc] initWithString:text attributes:normalAttrs]];
        [highlight addObject:[[NSAttributedString alloc] initWithString:text attributes:highlightAttrs]];
    }

    _normalAttrLines = normal;
    _highlightAttrLines = highlight;
}

- (double)computeTargetScrollOffsetForResult:(const openlyrics::SyncResult &)result {
    const double centerLine = (result.lineIndex >= 0) ? (result.lineIndex + result.progress) : 0.0;
    const double contentCenterY = centerLine * _lineHeight + _lineHeight / 2.0;
    return contentCenterY - self.bounds.size.height / 2.0;
}

#pragma mark - 内部：缓动定时器

- (void)startAnimationTimerIfNeeded {
    if (_animTimer != nil) return;
    // block 版 NSTimer + __weak self：与 LyricPanelController 的 positionTimer 同一模式，
    // 避免定时器强引用 self 造成循环引用；view 被移出层级 dealloc 后，
    // weakSelf 变 nil，下一次 tick 自行 invalidate。
    __weak __typeof__(self) weakSelf = self;
    _animTimer = [NSTimer scheduledTimerWithTimeInterval:kAnimTickInterval
                                                   repeats:YES
                                                     block:^(NSTimer *timer) {
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) {
            [timer invalidate];
            return;
        }
        [strongSelf tickAnimation];
    }];
}

- (void)stopAnimationTimer {
    [_animTimer invalidate];
    _animTimer = nil;
}

- (void)tickAnimation {
    const double delta = _targetScrollOffset - _scrollOffset;
    if (std::fabs(delta) < kScrollSnapThreshold) {
        if (_scrollOffset != _targetScrollOffset) {
            _scrollOffset = _targetScrollOffset;
            self.needsDisplay = YES;
        }
        return;
    }
    _scrollOffset += delta * kScrollEasing;
    self.needsDisplay = YES;
}

#pragma mark - 绘制

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
    const NSRect bounds = self.bounds;
    [BackgroundColor() set];
    NSRectFill(bounds);

    if (_normalAttrLines.count == 0) {
        [self drawPlaceholderInBounds:bounds];
        return;
    }

    if (!_data.synced) {
        [self drawStaticLinesInBounds:bounds];
        return;
    }

    [self drawSyncedLinesInBounds:bounds];
}

- (void)drawPlaceholderInBounds:(NSRect)bounds {
    [self drawAttrString:_placeholderAttr centeredInRect:bounds];
}

- (void)drawStaticLinesInBounds:(NSRect)bounds {
    // 无时标的纯文本歌词：从顶部按原始顺序静态列出，不高亮、不滚动。
    CGFloat y = 8.0;
    for (NSAttributedString *line in _normalAttrLines) {
        if (y > bounds.size.height) break;
        NSRect rowRect = NSMakeRect(8.0, y, bounds.size.width - 16.0, _lineHeight);
        [self drawAttrString:line centeredInRect:rowRect];
        y += _lineHeight;
    }
}

- (void)drawSyncedLinesInBounds:(NSRect)bounds {
    const NSInteger count = static_cast<NSInteger>(_normalAttrLines.count);
    for (NSInteger i = 0; i < count; i++) {
        const CGFloat rowTop = i * _lineHeight - _scrollOffset;
        if (rowTop + _lineHeight < 0 || rowTop > bounds.size.height) continue;  // 屏幕外，跳过绘制

        NSAttributedString *line = (i == _syncResult.lineIndex) ? _highlightAttrLines[i] : _normalAttrLines[i];
        NSRect rowRect = NSMakeRect(0.0, rowTop, bounds.size.width, _lineHeight);
        [self drawAttrString:line centeredInRect:rowRect];
    }
}

- (void)drawAttrString:(NSAttributedString *)str centeredInRect:(NSRect)rect {
    if (str.length == 0) return;
    const NSSize size = str.size;
    CGFloat x = rect.origin.x + (rect.size.width - size.width) / 2.0;
    if (x < rect.origin.x) x = rect.origin.x;  // 超宽兜底：不裁剪，退化为左对齐
    const CGFloat y = rect.origin.y + (rect.size.height - size.height) / 2.0;
    [str drawAtPoint:NSMakePoint(x, y)];
}

@end
