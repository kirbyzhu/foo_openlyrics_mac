#import "LyricView.h"
#import "stdafx.h"

#include <algorithm>
#include <cmath>

static NSString *const kPlaceholderNoLyrics = @"无歌词";
static const double kScrollEasing = 0.22;
static const double kScrollSnapThreshold = 0.25;
static const NSTimeInterval kAnimTickInterval = 0.06;

static NSColor *colorFromHex(const std::string& hex, NSColor *fallback) {
    if (hex.size() < 7 || hex[0] != '#') return fallback;
    unsigned int r = 0, g = 0, b = 0;
    sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
    return [NSColor colorWithRed:r/255.0 green:g/255.0 blue:b/255.0 alpha:1.0];
}

static NSTextAlignment alignmentFromString(const std::string& s) {
    if (s == "left") return NSTextAlignmentLeft;
    if (s == "right") return NSTextAlignmentRight;
    return NSTextAlignmentCenter;
}

@implementation LyricView {
    openlyrics::LyricData _data;
    openlyrics::SyncResult _syncResult;
    openlyrics::DisplayConfig _displayCfg;
    NSArray<NSAttributedString *> *_normalAttrLines;
    NSArray<NSAttributedString *> *_highlightAttrLines;
    NSAttributedString *_placeholderAttr;
    NSString *_titleText;
    NSAttributedString *_titleAttr;

    double _scrollOffset;
    double _targetScrollOffset;
    NSMutableArray<NSNumber *> *_rowHeights;
    BOOL _rowHeightsDirty;
    CGFloat _lastWidthForRowHeights;

    NSTimer *_animTimer;
    BOOL _transparentBackground;
    NSInteger _maxLines;
}

@synthesize transparentBackground = _transparentBackground;
@synthesize maxLines = _maxLines;

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self != nil) {
        _displayCfg = openlyrics::DisplayConfig{};
        _normalAttrLines = @[];
        _highlightAttrLines = @[];
        _rowHeights = [NSMutableArray array];
        _rowHeightsDirty = YES;
        _lastWidthForRowHeights = 0;
        _maxLines = 0;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)dealloc { [_animTimer invalidate]; }

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
    _rowHeightsDirty = YES;

    if (_data.synced && _normalAttrLines.count > 0) {
        [self startAnimationTimerIfNeeded];
    } else {
        [self stopAnimationTimer];
    }

    self.needsDisplay = YES;
}

- (void)setSyncResult:(const openlyrics::SyncResult &)result {
    if (!_data.synced || _normalAttrLines.count == 0) return;

    _syncResult = result;
    _targetScrollOffset = [self computeTargetScrollOffsetForResult:result];

    if (std::fabs(_targetScrollOffset - _scrollOffset) > self.bounds.size.height) {
        _scrollOffset = _targetScrollOffset;
    }
    self.needsDisplay = YES;
}

- (void)applyDisplayConfig:(const openlyrics::DisplayConfig &)config {
    _displayCfg = config;
    [self rebuildCachedLines];
    [self rebuildTitleAttr];
    _rowHeightsDirty = YES;
    self.needsDisplay = YES;
}

- (void)stopAnimation {
    [self stopAnimationTimer];
}

- (void)invalidateRowHeights {
    _rowHeightsDirty = YES;
    self.needsDisplay = YES;
}

- (void)setTitleText:(NSString *)text {
    NSString *t = (text.length > 0) ? text : nil;
    if ((t == nil && _titleText == nil) || [t isEqualToString:_titleText]) return;
    _titleText = [t copy];
    [self rebuildTitleAttr];
    // 标题出现/消失会改变歌词区高度，需重算滚动居中目标
    if (_data.synced && _normalAttrLines.count > 0) {
        _targetScrollOffset = [self computeTargetScrollOffsetForResult:_syncResult];
    }
    self.needsDisplay = YES;
}

// 标题栏高度：有标题时为字号 + 上下留白，无标题为 0（歌词区占满）。
- (CGFloat)titleHeight {
    if (_titleAttr == nil || _titleAttr.length == 0) return 0.0;
    return ceil(_displayCfg.fontSize + 10.0);
}

- (void)rebuildTitleAttr {
    if (_titleText.length == 0) { _titleAttr = nil; return; }
    NSColor *color = colorFromHex(_displayCfg.normalColor,
        [NSColor colorWithCalibratedWhite:0.85 alpha:1.0]);
    NSMutableParagraphStyle *ps = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
    ps.alignment = alignmentFromString(_displayCfg.alignment);
    ps.lineBreakMode = NSLineBreakByTruncatingTail;  // 单行超长省略，标题不换行
    _titleAttr = [[NSAttributedString alloc] initWithString:_titleText attributes:@{
        NSFontAttributeName : [self normalFont],
        NSForegroundColorAttributeName : color,
        NSParagraphStyleAttributeName : ps,
    }];
}

#pragma mark - 内部：数据准备

- (NSFont *)normalFont {
    return [NSFont fontWithName:[NSString stringWithUTF8String:_displayCfg.fontName.c_str()]
                           size:_displayCfg.fontSize]
        ?: [NSFont systemFontOfSize:_displayCfg.fontSize];
}

- (NSFont *)highlightFont {
    return [NSFont fontWithName:[NSString stringWithUTF8String:_displayCfg.fontName.c_str()]
                           size:_displayCfg.fontSize * _displayCfg.highlightScale]
        ?: [NSFont boldSystemFontOfSize:_displayCfg.fontSize * _displayCfg.highlightScale];
}

- (void)rebuildCachedLines {
    const size_t count = _data.lines.size();
    NSMutableArray<NSAttributedString *> *normal = [NSMutableArray arrayWithCapacity:count];
    NSMutableArray<NSAttributedString *> *highlight = [NSMutableArray arrayWithCapacity:count];

    NSColor *normalColor = colorFromHex(_displayCfg.normalColor,
        [NSColor colorWithCalibratedWhite:0.62 alpha:1.0]);
    NSColor *hlColor = colorFromHex(_displayCfg.highlightColor,
        [NSColor colorWithCalibratedRed:1.0 green:0.84 blue:0.35 alpha:1.0]);

    NSMutableParagraphStyle *ps = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
    ps.alignment = alignmentFromString(_displayCfg.alignment);
    ps.lineSpacing = _displayCfg.lineSpacing;
    ps.lineBreakMode = NSLineBreakByCharWrapping;

    NSDictionary *normalAttrs = @{
        NSFontAttributeName : [self normalFont],
        NSForegroundColorAttributeName : normalColor,
        NSParagraphStyleAttributeName : ps,
    };
    NSDictionary *highlightAttrs = @{
        NSFontAttributeName : [self highlightFont],
        NSForegroundColorAttributeName : hlColor,
        NSParagraphStyleAttributeName : ps,
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

    NSColor *placeholderColor = colorFromHex(_displayCfg.normalColor,
        [NSColor colorWithCalibratedWhite:0.45 alpha:1.0]);
    _placeholderAttr = [[NSAttributedString alloc] initWithString:kPlaceholderNoLyrics attributes:@{
        NSFontAttributeName : [self normalFont],
        NSForegroundColorAttributeName : placeholderColor,
    }];
}

- (void)computeRowHeightsIfNeeded {
    const CGFloat width = self.bounds.size.width;
    if (!_rowHeightsDirty && width == _lastWidthForRowHeights) return;

    const NSInteger count = static_cast<NSInteger>(_normalAttrLines.count);
    NSMutableArray *heights = [NSMutableArray arrayWithCapacity:count];
    const CGFloat availWidth = MAX(width - 16.0, 20.0);

    // 按高亮（放大）字体测量行高：高亮行字号放大 highlightScale 倍，若按普通字体
    // 测高，放大加粗行会被垂直截断、换行显示不完整。统一用较大者保证任何行不截断。
    for (NSInteger i = 0; i < count; i++) {
        CGFloat h = [self heightForAttrString:_highlightAttrLines[i] width:availWidth];
        [heights addObject:@(h)];
    }

    _rowHeights = heights;
    _rowHeightsDirty = NO;
    _lastWidthForRowHeights = width;
}

- (CGFloat)heightForAttrString:(NSAttributedString *)str width:(CGFloat)width {
    if (str.length == 0) return _displayCfg.fontSize + _displayCfg.lineSpacing;
    NSSize sz = [str boundingRectWithSize:NSMakeSize(width, CGFLOAT_MAX)
                                  options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading]
                    .size;
    // boundingRectWithSize 返回紧凑包围盒，drawInRect 实际绘制可能略高，加 4px 缓冲
    return ceil(sz.height + 4.0);
}

- (double)computeTargetScrollOffsetForResult:(const openlyrics::SyncResult &)result {
    [self computeRowHeightsIfNeeded];
    if (_rowHeights.count == 0) return 0;

    const NSInteger idx = result.lineIndex;
    double yBefore = 0;
    for (NSInteger i = 0; i < idx && i < (NSInteger)_rowHeights.count; i++) {
        yBefore += [_rowHeights[i] doubleValue];
    }
    const double lineH = (idx >= 0 && idx < (NSInteger)_rowHeights.count)
        ? [_rowHeights[idx] doubleValue] : 0;
    const double centerLineY = yBefore + lineH * result.progress + lineH / 2.0;
    const double lyricHeight = self.bounds.size.height - [self titleHeight];
    return centerLineY - lyricHeight / 2.0;
}

#pragma mark - 内部：缓动定时器

- (void)startAnimationTimerIfNeeded {
    if (_animTimer != nil) return;
    __weak __typeof__(self) weakSelf = self;
    _animTimer = [NSTimer scheduledTimerWithTimeInterval:kAnimTickInterval repeats:YES block:^(NSTimer *timer) {
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) { [timer invalidate]; return; }
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
    if (!_transparentBackground) {
        [[NSColor controlBackgroundColor] set];
        NSRectFill(bounds);
    }

    const CGFloat titleH = [self titleHeight];
    const NSRect lyricRect = NSMakeRect(bounds.origin.x, bounds.origin.y + titleH,
                                        bounds.size.width, bounds.size.height - titleH);

    if (_normalAttrLines.count == 0) {
        [self drawAttrString:_placeholderAttr centeredInRect:lyricRect];
    } else {
        // 歌词裁剪到标题下方区域，防止上滚的歌词覆盖标题栏
        [NSGraphicsContext saveGraphicsState];
        NSRectClip(lyricRect);
        if (!_data.synced) {
            [self drawStaticLinesInRect:lyricRect];
        } else {
            [self drawSyncedLinesInRect:lyricRect];
        }
        [NSGraphicsContext restoreGraphicsState];
    }

    if (titleH > 0) {
        [self drawTitleInRect:NSMakeRect(bounds.origin.x, bounds.origin.y, bounds.size.width, titleH)];
    }
}

- (void)drawTitleInRect:(NSRect)rect {
    if (_titleAttr == nil || _titleAttr.length == 0) return;
    const NSSize sz = _titleAttr.size;
    const CGFloat pad = 6.0;
    NSRect textRect = NSMakeRect(rect.origin.x + pad,
                                 rect.origin.y + (rect.size.height - sz.height) / 2.0,
                                 rect.size.width - 2 * pad, sz.height);
    [_titleAttr drawInRect:textRect];  // 段落样式 truncatingTail 处理超长单行
}

- (void)drawStaticLinesInRect:(NSRect)rect {
    [self computeRowHeightsIfNeeded];
    CGFloat y = rect.origin.y + 8.0;
    for (NSInteger i = 0; i < (NSInteger)_rowHeights.count; i++) {
        if (y > NSMaxY(rect)) break;
        CGFloat h = [_rowHeights[i] doubleValue];
        NSRect rowRect = NSMakeRect(rect.origin.x + 8.0, y, rect.size.width - 16.0, h);
        [self drawAttrString:_normalAttrLines[i] inRect:rowRect];
        y += h;
    }
}

- (void)drawSyncedLinesInRect:(NSRect)rect {
    [self computeRowHeightsIfNeeded];
    const NSInteger count = static_cast<NSInteger>(_normalAttrLines.count);
    if (count == 0) return;

    NSInteger fromLine = 0;
    NSInteger toLine = count;
    if (_maxLines > 0) {
        const NSInteger half = _maxLines / 2;
        fromLine = _syncResult.lineIndex - half;
        toLine = _syncResult.lineIndex + half + (_maxLines % 2);
        if (fromLine < 0) fromLine = 0;
        if (toLine > count) toLine = count;
    }

    // 累加行高计算每行的 y 偏移，基准平移到歌词区顶部 rect.origin.y
    double yAccum = 0;
    for (NSInteger i = 0; i < count; i++) {
        const double h = [_rowHeights[i] doubleValue];
        const double rowTop = rect.origin.y + yAccum - _scrollOffset;
        yAccum += h;

        if (i < fromLine || i >= toLine) continue;
        if (rowTop + h < NSMinY(rect) || rowTop > NSMaxY(rect)) continue;

        NSAttributedString *line = (i == _syncResult.lineIndex) ? _highlightAttrLines[i] : _normalAttrLines[i];
        NSRect rowRect = NSMakeRect(rect.origin.x + 8.0, rowTop, rect.size.width - 16.0, h);
        [self drawAttrString:line inRect:rowRect];
    }
}

- (void)drawAttrString:(NSAttributedString *)str inRect:(NSRect)rect {
    if (str.length == 0) return;
    [str drawInRect:rect];
}

- (void)drawAttrString:(NSAttributedString *)str centeredInRect:(NSRect)rect {
    if (str.length == 0) return;
    const NSSize size = str.size;
    CGFloat x;
    NSTextAlignment align = alignmentFromString(_displayCfg.alignment);
    if (align == NSTextAlignmentLeft) {
        x = rect.origin.x + 8.0;
    } else if (align == NSTextAlignmentRight) {
        x = rect.origin.x + rect.size.width - size.width - 8.0;
    } else {
        x = rect.origin.x + (rect.size.width - size.width) / 2.0;
    }
    if (x < rect.origin.x) x = rect.origin.x;
    const CGFloat y = rect.origin.y + (rect.size.height - size.height) / 2.0;
    [str drawAtPoint:NSMakePoint(x, y)];
}

#pragma mark - 窗口尺寸变化

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    if (fabs(newSize.width - _lastWidthForRowHeights) > 0.5) {
        _rowHeightsDirty = YES;
        // 更新滚动目标，因为行高可能因换行改变
        if (_data.synced && _normalAttrLines.count > 0) {
            _targetScrollOffset = [self computeTargetScrollOffsetForResult:_syncResult];
        }
        self.needsDisplay = YES;
    }
}

@end
