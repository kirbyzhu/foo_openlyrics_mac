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

    double _scrollOffset;
    double _targetScrollOffset;
    CGFloat _lineHeight;

    NSTimer *_animTimer;
    BOOL _transparentBackground;
}

@synthesize transparentBackground = _transparentBackground;

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self != nil) {
        _displayCfg = openlyrics::DisplayConfig{};
        _lineHeight = 30.0;
        _normalAttrLines = @[];
        _highlightAttrLines = @[];
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
    _lineHeight = config.fontSize * 1.8 + config.lineSpacing;
    [self rebuildCachedLines];
    self.needsDisplay = YES;
}

- (void)stopAnimation {
    [self stopAnimationTimer];
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
    // 行距通过 NSParagraphStyle 控制行间距
    ps.lineSpacing = _displayCfg.lineSpacing;

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

    // 重建占位文本
    NSColor *placeholderColor = colorFromHex(_displayCfg.normalColor,
        [NSColor colorWithCalibratedWhite:0.45 alpha:1.0]);
    _placeholderAttr = [[NSAttributedString alloc] initWithString:kPlaceholderNoLyrics attributes:@{
        NSFontAttributeName : [self normalFont],
        NSForegroundColorAttributeName : placeholderColor,
    }];
}

- (double)computeTargetScrollOffsetForResult:(const openlyrics::SyncResult &)result {
    const double centerLine = (result.lineIndex >= 0) ? (result.lineIndex + result.progress) : 0.0;
    const double contentCenterY = centerLine * _lineHeight + _lineHeight / 2.0;
    return contentCenterY - self.bounds.size.height / 2.0;
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

    if (_normalAttrLines.count == 0) {
        [self drawAttrString:_placeholderAttr centeredInRect:bounds];
        return;
    }

    if (!_data.synced) {
        [self drawStaticLinesInBounds:bounds];
        return;
    }

    [self drawSyncedLinesInBounds:bounds];
}

- (void)drawStaticLinesInBounds:(NSRect)bounds {
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
        if (rowTop + _lineHeight < 0 || rowTop > bounds.size.height) continue;

        NSAttributedString *line = (i == _syncResult.lineIndex) ? _highlightAttrLines[i] : _normalAttrLines[i];
        NSRect rowRect = NSMakeRect(0.0, rowTop, bounds.size.width, _lineHeight);
        [self drawAttrString:line centeredInRect:rowRect];
    }
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

@end
