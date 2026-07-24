// DesktopLyricsController.mm
// foo_openlyrics_mac —— Plan 7 Task 2：桌面歌词浮动透明窗口控制器实现。
#import "DesktopLyricsController.h"
#import "stdafx.h"

#import "LyricView.h"
#import "TagIOAdapter.h"
#import "FileSystemAdapter.h"
#import "HttpAdapter.h"
#import "CryptoAdapter.h"
#import "ConfigAdapter.h"
#import "PreferencesViewController.h"

#include "sources/TagSource.h"
#include "sources/LocalFileSource.h"
#include "sources/LrcLibProvider.h"
#include "sources/NetEaseProvider.h"
#include "sources/QQMusicProvider.h"
#include "pipeline/SearchPipeline.h"
#include "pipeline/SearchCoordinator.h"
#include "matching/Matcher.h"
#include "store/LyricStore.h"
#include "sync/SyncEngine.h"
#include "parser/LrcParser.h"
#include "parser/LrcSerializer.h"
#include "model/LyricData.h"
#include "config/AppConfig.h"
#include "ports/CancelToken.h"
#include <sstream>

static const NSTimeInterval kSyncTickInterval = 0.06;
static const CGFloat kScrubPixelsPerSecond = 20.0;
static const CGFloat kHudFadeDelay = 1.5;
static const CGFloat kMinPanelWidth = 200.0;
static const CGFloat kMinPanelHeight = 60.0;
static const CGFloat kEdgeResizeMargin = 10.0;
static const NSTimeInterval kSaveDebounce = 0.3;
static const CGFloat kCornerRadius = 14.0;      // 面板圆角半径，贴近 macOS 小部件风格
static const CGFloat kContentInset = 6.0;       // 歌词视图相对面板的内边距，避开圆角
static const NSTimeInterval kHoverShowDelay = 1.0;  // 悬停多久后才显现背景，避免鼠标掠过误触发
static const CGFloat kTapPixelsPerStep = 40.0;       // 触控板精确滚动每累积此像素打轴一步

typedef NS_ENUM(NSInteger, DeskMenuTag) {
    DeskMenuTagReSearchLrcLib = 1,
    DeskMenuTagReSearchNetEase,
    DeskMenuTagReSearchQQ,
    DeskMenuTagDeleteLyric,
    DeskMenuTagMaxLines3,
    DeskMenuTagMaxLines4,
    DeskMenuTagMaxLines5,
    DeskMenuTagMaxLines6,
    DeskMenuTagMaxLines7,
    DeskMenuTagOffsetPlus1000,
    DeskMenuTagOffsetPlus500,
    DeskMenuTagOffsetReset,
    DeskMenuTagOffsetMinus500,
    DeskMenuTagOffsetMinus1000,
    DeskMenuTagPlayPause,
    DeskMenuTagStop,
    DeskMenuTagPrevious,
    DeskMenuTagNext,
    DeskMenuTagPreferences,
    DeskMenuTagQuitDesktopLyrics,
    DeskMenuTagQuitFoobar,
};

static NSString *titleForMaxLinesTag(DeskMenuTag tag) {
    switch (tag) {
        case DeskMenuTagMaxLines3: return @"3 行";
        case DeskMenuTagMaxLines4: return @"4 行";
        case DeskMenuTagMaxLines5: return @"5 行";
        case DeskMenuTagMaxLines6: return @"6 行";
        case DeskMenuTagMaxLines7: return @"7 行";
        default: return @"";
    }
}

static NSInteger maxLinesValueForTag(DeskMenuTag tag) {
    switch (tag) {
        case DeskMenuTagMaxLines3: return 3;
        case DeskMenuTagMaxLines4: return 4;
        case DeskMenuTagMaxLines5: return 5;
        case DeskMenuTagMaxLines6: return 6;
        case DeskMenuTagMaxLines7: return 7;
        default: return 3;
    }
}

static DeskMenuTag maxLinesTagForValue(NSInteger v) {
    switch (v) {
        case 3: return DeskMenuTagMaxLines3;
        case 4: return DeskMenuTagMaxLines4;
        case 5: return DeskMenuTagMaxLines5;
        case 6: return DeskMenuTagMaxLines6;
        case 7: return DeskMenuTagMaxLines7;
        default: return DeskMenuTagMaxLines3;
    }
}

// 命中的窗口边缘，用于边缘拖拽缩放。视图 isFlipped=YES，故
// Top 对应屏幕坐标 max-y、Bottom 对应 origin.y。可组合（角落同时命中两边）。
typedef NS_OPTIONS(NSUInteger, DeskEdge) {
    DeskEdgeNone   = 0,
    DeskEdgeLeft   = 1 << 0,
    DeskEdgeRight  = 1 << 1,
    DeskEdgeTop    = 1 << 2,
    DeskEdgeBottom = 1 << 3,
};

// 允许成为 key 窗口以接收方向键；保持不成为 main，避免整体夺取应用主窗口地位。
@interface DeskLyricsPanel : NSPanel
@end

@implementation DeskLyricsPanel
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

// 负责鼠标拖拽：普通=移动、Command=缩放、Option=偏移微调、边缘=系统 resize。
// 任何拖拽模式均显示非透明边框，解决透明窗口无边界反馈问题。
// 提供右键菜单：播放控制（一级）、重新搜索（按在线源）、删除当前歌词文件、显示行数、偏移调整。
@interface DeskLyricsContent : NSView
@property(nonatomic, copy) void (^onOffsetDelta)(int64_t deltaMs);
@property(nonatomic, copy) void (^onOffsetCommit)(int64_t totalDeltaMs);
@property(nonatomic, copy) void (^onTapAnchor)(void);        // 右键锁定当前句为锚点
@property(nonatomic, copy) void (^onTapStep)(int dir);       // 上下键/滚轮打轴一步：+1 前进/-1 回退
@property(nonatomic, copy) void (^onMoveTo)(NSPoint origin);
@property(nonatomic, copy) void (^onResizeFrame)(NSRect frame);
@property(nonatomic, copy) void (^onResizeCommit)(void);

// 右键菜单回调
@property(nonatomic, copy) void (^onReSearchSource)(NSString *sourceKey);
@property(nonatomic, copy) void (^onDeleteLyric)(void);
@property(nonatomic, copy) void (^onSetMaxLines)(NSInteger lines);
@property(nonatomic, copy) void (^onAdjustOffset)(int64_t deltaMs);
@property(nonatomic, copy) void (^onResetOffset)(void);
@property(nonatomic, copy) void (^onPlayPause)(void);
@property(nonatomic, copy) void (^onStop)(void);
@property(nonatomic, copy) void (^onPrevious)(void);
@property(nonatomic, copy) void (^onNext)(void);
@property(nonatomic, copy) void (^onOpenPreferences)(void);
@property(nonatomic, copy) void (^onQuitDesktopLyrics)(void);
@property(nonatomic, copy) void (^onQuitFoobar)(void);

// 当前显示行数，右键菜单打勾用
@property(nonatomic, assign) NSInteger currentMaxLines;
// 当前歌词是否有可删除的本地来源文件，控制"删除当前歌词文件"是否可点
@property(nonatomic, assign) BOOL canDeleteLyric;

// 在面板中央弹出一条瞬时提示（复用 HUD），短暂显示后自动淡出。
- (void)showMessage:(NSString *)text;
@end

@implementation DeskLyricsContent {
    NSPoint _mouseDownPos;
    BOOL _isDragging;
    BOOL _isScrubbing;
    BOOL _isResizing;    // 系统边缘 resize
    BOOL _isCmdResizing; // Command 键缩放
    int64_t _scrubDeltaMs;
    NSTextField *_hudLabel;

    BOOL _dragBorderVisible;

    DeskEdge _resizeEdges;          // 边缘缩放命中的边
    NSRect _resizeStartFrame;       // 边缘缩放起始窗口 frame
    NSPoint _resizeStartScreenMouse;// 边缘缩放起始鼠标（屏幕坐标）

    NSPoint _dragStartScreenMouse;  // 移动起始鼠标（屏幕坐标）
    NSPoint _dragStartFrameOrigin;  // 移动起始窗口原点

    NSVisualEffectView *_bgView;    // 圆角毛玻璃背景，仅悬停/拖拽时显现
    NSTrackingArea *_trackingArea;
    BOOL _hovering;                 // 鼠标悬停中
    BOOL _hoverBgActive;            // 悬停已满 kHoverShowDelay，允许因悬停显现背景
    BOOL _interacting;              // 拖拽/缩放/偏移微调进行中
    BOOL _bgShown;                  // 背景当前是否可见，去抖动画

    CGFloat _tapAccumPx;            // 触控板精确滚动的像素累积，达阈值打轴一步
}

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self != nil) {
        self.wantsLayer = YES;
        self.layer.masksToBounds = YES;
        self.layer.cornerRadius = kCornerRadius;

        // 毛玻璃背景铺满面板，圆角 + 极细高光描边，随系统外观自适应明暗。
        _bgView = [[NSVisualEffectView alloc] initWithFrame:self.bounds];
        _bgView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        _bgView.material = NSVisualEffectMaterialHUDWindow;
        _bgView.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        _bgView.state = NSVisualEffectStateActive;
        _bgView.wantsLayer = YES;
        _bgView.layer.cornerRadius = kCornerRadius;
        _bgView.layer.masksToBounds = YES;
        _bgView.layer.borderWidth = 0.5;
        _bgView.layer.borderColor = [NSColor colorWithCalibratedWhite:1.0 alpha:0.14].CGColor;
        _bgView.alphaValue = 0.0;  // 默认全透明，仅悬停/拖拽时淡入
        [self addSubview:_bgView positioned:NSWindowBelow relativeTo:nil];

        _hudLabel = [[NSTextField alloc] initWithFrame:NSZeroRect];
        _hudLabel.editable = NO;
        _hudLabel.selectable = NO;
        _hudLabel.bordered = NO;
        _hudLabel.backgroundColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.55];
        _hudLabel.textColor = NSColor.whiteColor;
        _hudLabel.font = [NSFont systemFontOfSize:24];
        _hudLabel.alignment = NSTextAlignmentCenter;
        _hudLabel.wantsLayer = YES;
        _hudLabel.layer.cornerRadius = 8.0;
        _hudLabel.alphaValue = 0.0;
        _hudLabel.hidden = YES;
        [self addSubview:_hudLabel];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)resetCursorRects {
    [super resetCursorRects];
    NSRect b = self.bounds;
    [self addCursorRect:NSMakeRect(0, 0, b.size.width, kEdgeResizeMargin)
                 cursor:[NSCursor resizeUpDownCursor]];
    [self addCursorRect:NSMakeRect(0, b.size.height - kEdgeResizeMargin, b.size.width, kEdgeResizeMargin)
                 cursor:[NSCursor resizeUpDownCursor]];
    [self addCursorRect:NSMakeRect(0, 0, kEdgeResizeMargin, b.size.height)
                 cursor:[NSCursor resizeLeftRightCursor]];
    [self addCursorRect:NSMakeRect(b.size.width - kEdgeResizeMargin, 0, kEdgeResizeMargin, b.size.height)
                 cursor:[NSCursor resizeLeftRightCursor]];
}

- (BOOL)mouseDownCanMoveWindow { return NO; }

- (DeskEdge)edgesForPoint:(NSPoint)point {
    NSRect b = self.bounds;
    DeskEdge edges = DeskEdgeNone;
    if (point.y <= kEdgeResizeMargin)                   edges |= DeskEdgeTop;
    if (b.size.height - point.y <= kEdgeResizeMargin)    edges |= DeskEdgeBottom;
    if (point.x <= kEdgeResizeMargin)                    edges |= DeskEdgeLeft;
    if (b.size.width - point.x <= kEdgeResizeMargin)     edges |= DeskEdgeRight;
    return edges;
}

- (BOOL)isPointNearEdge:(NSPoint)point {
    return [self edgesForPoint:point] != DeskEdgeNone;
}

#pragma mark - 背景显隐（悬停/拖拽）

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_trackingArea != nil) [self removeTrackingArea:_trackingArea];
    _trackingArea = [[NSTrackingArea alloc]
        initWithRect:self.bounds
             options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect
               owner:self
            userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)mouseEntered:(NSEvent *)event {
    (void)event;
    _hovering = YES;
    // 悬停满 kHoverShowDelay 才显现背景：鼠标短暂掠过不触发，避免遮挡窗口下方的桌面内容。
    [self performSelector:@selector(hoverDwellElapsed) withObject:nil afterDelay:kHoverShowDelay];
}

- (void)mouseExited:(NSEvent *)event {
    (void)event;
    _hovering = NO;
    _hoverBgActive = NO;
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(hoverDwellElapsed)
                                               object:nil];
    [self updateBackgroundShown];
}

- (void)hoverDwellElapsed {
    if (!_hovering) return;   // 已提前移出则作废
    _hoverBgActive = YES;
    [self updateBackgroundShown];
}

// 悬停满时长或正在交互任一成立即显现磨玻璃背景，否则回到全透明；带淡入淡出并刷新窗口阴影。
- (void)updateBackgroundShown {
    BOOL shouldShow = _hoverBgActive || _interacting;
    if (shouldShow == _bgShown) return;
    _bgShown = shouldShow;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *ctx) {
        ctx.duration = 0.18;
        ctx.allowsImplicitAnimation = YES;
        self->_bgView.animator.alphaValue = shouldShow ? 1.0 : 0.0;
    } completionHandler:^{
        [self.window invalidateShadow];
    }];
    [self.window invalidateShadow];
}

#pragma mark - 拖拽边框

- (void)showDragBorder {
    _interacting = YES;
    [self updateBackgroundShown];
    if (_dragBorderVisible) return;
    _dragBorderVisible = YES;
    self.layer.borderWidth = 3.0;
    self.layer.borderColor = [NSColor colorWithCalibratedWhite:0.7 alpha:0.75].CGColor;
}

- (void)hideDragBorder {
    _interacting = NO;
    [self updateBackgroundShown];
    if (!_dragBorderVisible) return;
    _dragBorderVisible = NO;
    self.layer.borderWidth = 0.0;
}

#pragma mark - 鼠标事件

- (void)mouseDown:(NSEvent *)event {
    [self.window makeFirstResponder:self];
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    DeskEdge edges = [self edgesForPoint:loc];
    if (edges != DeskEdgeNone) {
        _isResizing = YES;
        _resizeEdges = edges;
        _resizeStartFrame = self.window.frame;
        _resizeStartScreenMouse = [NSEvent mouseLocation];
        [self showDragBorder];
        return;
    }
    _isResizing = NO;
    _isCmdResizing = NO;
    _mouseDownPos = [event locationInWindow];
    _dragStartScreenMouse = [NSEvent mouseLocation];
    _dragStartFrameOrigin = self.window.frame.origin;
    _isDragging = NO;
    _isScrubbing = NO;
    _scrubDeltaMs = 0;
}

- (void)mouseDragged:(NSEvent *)event {
    if (_isResizing) {
        [self performEdgeResize];
        return;
    }

    NSPoint cur = [event locationInWindow];
    CGFloat dx = cur.x - _mouseDownPos.x;
    CGFloat dy = cur.y - _mouseDownPos.y;

    // 首次判断拖拽方向
    if (!_isDragging && !_isScrubbing && !_isCmdResizing) {
        if (fabs(dx) < 3.0 && fabs(dy) < 3.0) return;
        NSEventModifierFlags flags = event.modifierFlags;
        if (flags & NSEventModifierFlagCommand) {
            _isCmdResizing = YES;
            // 记录整块起始 frame 与屏幕坐标起点：缩放中窗口尺寸变化会令 locationInWindow
            // 参考系漂移导致灵敏度失真，故一律用屏幕坐标做 1:1 位移。
            _resizeStartFrame = self.window.frame;
            _resizeStartScreenMouse = [NSEvent mouseLocation];
            [self showDragBorder];
            return;
        } else if (flags & NSEventModifierFlagOption) {
            _isScrubbing = YES;
        } else {
            _isDragging = YES;
        }
        [self showDragBorder];
    }

    if (_isCmdResizing) {
        // 固定左上角，模拟拖拽右下角：右移变宽、下移变高，屏幕坐标 1:1 无漂移。
        NSPoint m = [NSEvent mouseLocation];
        CGFloat dx = m.x - _resizeStartScreenMouse.x;
        CGFloat dy = m.y - _resizeStartScreenMouse.y;  // 屏幕坐标 y 向上，下移为负
        CGFloat newW = MAX(kMinPanelWidth,  _resizeStartFrame.size.width  + dx);
        CGFloat newH = MAX(kMinPanelHeight, _resizeStartFrame.size.height - dy);
        NSRect f = _resizeStartFrame;
        CGFloat fixedTop = _resizeStartFrame.origin.y + _resizeStartFrame.size.height;
        f.size.width = newW;
        f.size.height = newH;
        f.origin.y = fixedTop - newH;   // 顶边不动，向下扩展
        if (self.onResizeFrame) self.onResizeFrame(f);
        [self showResizeHudWithWidth:newW height:newH];
    } else if (_isScrubbing) {
        int64_t delta = static_cast<int64_t>(-dy * (1000.0 / kScrubPixelsPerSecond));
        _scrubDeltaMs += delta;
        _mouseDownPos = cur;
        if (self.onOffsetDelta) self.onOffsetDelta(_scrubDeltaMs);
        [self showOffsetHudWithDelta:_scrubDeltaMs];
    } else if (_isDragging) {
        // 用屏幕坐标做绝对定位，避免 setFrameOrigin 后窗口坐标参考系漂移。
        NSPoint m = [NSEvent mouseLocation];
        NSPoint target = NSMakePoint(_dragStartFrameOrigin.x + (m.x - _dragStartScreenMouse.x),
                                     _dragStartFrameOrigin.y + (m.y - _dragStartScreenMouse.y));
        if (self.onMoveTo) self.onMoveTo(target);
    }
}

// 边缘拖拽缩放：以屏幕坐标计算位移，按命中的边调整 frame 的原点与尺寸。
// 命中角落时两边同时生效。左/下边缘缩放需同步移动原点，触及最小尺寸时钳制原点。
- (void)performEdgeResize {
    NSPoint m = [NSEvent mouseLocation];
    CGFloat dx = m.x - _resizeStartScreenMouse.x;
    CGFloat dy = m.y - _resizeStartScreenMouse.y;
    NSRect f = _resizeStartFrame;

    if (_resizeEdges & DeskEdgeRight) {
        f.size.width = _resizeStartFrame.size.width + dx;
    }
    if (_resizeEdges & DeskEdgeLeft) {
        f.size.width = _resizeStartFrame.size.width - dx;
        f.origin.x   = _resizeStartFrame.origin.x + dx;
    }
    if (_resizeEdges & DeskEdgeTop) {
        f.size.height = _resizeStartFrame.size.height + dy;
    }
    if (_resizeEdges & DeskEdgeBottom) {
        f.size.height = _resizeStartFrame.size.height - dy;
        f.origin.y    = _resizeStartFrame.origin.y + dy;
    }

    if (f.size.width < kMinPanelWidth) {
        if (_resizeEdges & DeskEdgeLeft)
            f.origin.x = _resizeStartFrame.origin.x + (_resizeStartFrame.size.width - kMinPanelWidth);
        f.size.width = kMinPanelWidth;
    }
    if (f.size.height < kMinPanelHeight) {
        if (_resizeEdges & DeskEdgeBottom)
            f.origin.y = _resizeStartFrame.origin.y + (_resizeStartFrame.size.height - kMinPanelHeight);
        f.size.height = kMinPanelHeight;
    }

    if (self.onResizeFrame) self.onResizeFrame(f);
    [self showResizeHudWithWidth:f.size.width height:f.size.height];
}

- (void)mouseUp:(NSEvent *)event {
    (void)event;
    if (_isResizing) {
        _isResizing = NO;
        _resizeEdges = DeskEdgeNone;
        if (self.onResizeCommit) self.onResizeCommit();
        [self hideDragBorder];
        [self fadeHud];
        return;
    }
    if (_isCmdResizing) {
        _isCmdResizing = NO;
        if (self.onResizeCommit) self.onResizeCommit();
    }
    if (_isScrubbing && self.onOffsetCommit) {
        self.onOffsetCommit(_scrubDeltaMs);
    }
    _isDragging = NO;
    _isScrubbing = NO;
    [self hideDragBorder];
    [self fadeHud];
}

- (void)showOffsetHudWithDelta:(int64_t)deltaMs {
    double sec = deltaMs / 1000.0;
    _hudLabel.stringValue = [NSString stringWithFormat:@"%+.2fs", sec];
    [self layoutHud];
}

- (void)showResizeHudWithWidth:(CGFloat)w height:(CGFloat)h {
    _hudLabel.stringValue = [NSString stringWithFormat:@"%.0f × %.0f", w, h];
    [self layoutHud];
}

- (void)showMessage:(NSString *)text {
    _hudLabel.stringValue = text;
    [self layoutHud];
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(fadeHud) object:nil];
    [self performSelector:@selector(fadeHud) withObject:nil afterDelay:0.8];
}

- (void)layoutHud {
    [_hudLabel sizeToFit];
    NSSize ls = _hudLabel.frame.size;
    CGFloat hw = ls.width + 24.0;
    CGFloat hh = ls.height + 8.0;
    NSRect b = self.bounds;
    _hudLabel.frame = NSMakeRect((b.size.width - hw) / 2.0, b.size.height * 0.25, hw, hh);
    _hudLabel.hidden = NO;
    _hudLabel.alphaValue = 0.85;
}

- (void)fadeHud {
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *ctx) {
        ctx.duration = kHudFadeDelay;
        ctx.allowsImplicitAnimation = YES;
        self->_hudLabel.animator.alphaValue = 0.0;
    } completionHandler:^{
        self->_hudLabel.hidden = YES;
    }];
}

#pragma mark - 滚轮/方向键打轴

- (void)scrollWheel:(NSEvent *)event {
    // 拖拽/缩放/边缘调整进行中时不响应，交回系统
    if (_isDragging || _isScrubbing || _isResizing || _isCmdResizing) {
        [super scrollWheel:event];
        return;
    }

    CGFloat dy = event.deltaY;
    if (fabs(dy) < 0.01) return;

    int steps = 0;
    if (event.hasPreciseScrollingDeltas) {
        // 触控板：按像素累积，达阈值一步；下滑(dy<0)->前进(dir=+1)
        _tapAccumPx += dy;
        while (_tapAccumPx <= -kTapPixelsPerStep) { _tapAccumPx += kTapPixelsPerStep; steps += 1; }
        while (_tapAccumPx >= kTapPixelsPerStep)  { _tapAccumPx -= kTapPixelsPerStep; steps -= 1; }
    } else {
        // 传统滚轮：每格一步，下滚(dy<0)->前进
        steps = (dy < 0) ? 1 : -1;
    }

    if (steps != 0 && self.onTapStep) self.onTapStep(steps > 0 ? 1 : -1);
}

- (BOOL)acceptsFirstResponder { return YES; }

- (void)keyDown:(NSEvent *)event {
    NSString *chars = event.charactersIgnoringModifiers;
    if (chars.length == 1) {
        unichar c = [chars characterAtIndex:0];
        if (c == NSRightArrowFunctionKey) {
            if (self.onTapAnchor) self.onTapAnchor();   // 右键：锁定当前句为锚点
            return;
        }
        if (c == NSDownArrowFunctionKey) {
            if (self.onTapStep) self.onTapStep(1);      // 下键：前进并写时标
            return;
        }
        if (c == NSUpArrowFunctionKey) {
            if (self.onTapStep) self.onTapStep(-1);     // 上键：回退撤销
            return;
        }
    }
    [super keyDown:event];
}

#pragma mark - 右键菜单

- (void)rightMouseDown:(NSEvent *)event {
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"桌面歌词"];
    menu.autoenablesItems = NO;

    // --- 播放控制（一级菜单）---
    NSMenuItem *pp = [[NSMenuItem alloc] initWithTitle:@"播放/暂停"
                                                action:@selector(handleMenuItem:)
                                         keyEquivalent:@""];
    pp.tag = DeskMenuTagPlayPause; pp.target = self;
    NSMenuItem *st = [[NSMenuItem alloc] initWithTitle:@"停止"
                                                action:@selector(handleMenuItem:)
                                         keyEquivalent:@""];
    st.tag = DeskMenuTagStop; st.target = self;
    NSMenuItem *pr = [[NSMenuItem alloc] initWithTitle:@"上一首"
                                                action:@selector(handleMenuItem:)
                                         keyEquivalent:@""];
    pr.tag = DeskMenuTagPrevious; pr.target = self;
    NSMenuItem *nx = [[NSMenuItem alloc] initWithTitle:@"下一首"
                                                action:@selector(handleMenuItem:)
                                         keyEquivalent:@""];
    nx.tag = DeskMenuTagNext; nx.target = self;
    [menu addItem:pp];
    [menu addItem:st];
    [menu addItem:pr];
    [menu addItem:nx];

    [menu addItem:[NSMenuItem separatorItem]];

    // --- 重新搜索歌词（子菜单：三个在线源单选）---
    NSMenuItem *researchItem = [[NSMenuItem alloc] initWithTitle:@"重新搜索歌词"
                                                          action:nil
                                                   keyEquivalent:@""];
    NSMenu *researchSub = [[NSMenu alloc] initWithTitle:@""];
    struct { NSString *title; DeskMenuTag tag; } srcRows[] = {
        {@"LrcLib", DeskMenuTagReSearchLrcLib},
        {@"网易云音乐", DeskMenuTagReSearchNetEase},
        {@"QQ音乐", DeskMenuTagReSearchQQ},
    };
    for (int i = 0; i < 3; i++) {
        NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:srcRows[i].title
                                                    action:@selector(handleMenuItem:)
                                             keyEquivalent:@""];
        it.tag = srcRows[i].tag;
        it.target = self;
        [researchSub addItem:it];
    }
    researchItem.submenu = researchSub;
    [menu addItem:researchItem];

    // --- 删除当前歌词文件 ---
    NSMenuItem *deleteItem = [[NSMenuItem alloc] initWithTitle:@"删除当前歌词文件"
                                                        action:@selector(handleMenuItem:)
                                                 keyEquivalent:@""];
    deleteItem.tag = DeskMenuTagDeleteLyric;
    deleteItem.target = self;
    deleteItem.enabled = _canDeleteLyric;
    [menu addItem:deleteItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // --- 显示行数 ---
    NSMenuItem *maxLinesItem = [[NSMenuItem alloc] initWithTitle:@"显示行数"
                                                          action:nil
                                                   keyEquivalent:@""];
    NSMenu *maxLinesSub = [[NSMenu alloc] initWithTitle:@""];
    DeskMenuTag lineTags[] = {DeskMenuTagMaxLines3, DeskMenuTagMaxLines4, DeskMenuTagMaxLines5,
                              DeskMenuTagMaxLines6, DeskMenuTagMaxLines7};
    for (int i = 0; i < 5; i++) {
        DeskMenuTag tag = lineTags[i];
        NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:titleForMaxLinesTag(tag)
                                                    action:@selector(handleMenuItem:)
                                             keyEquivalent:@""];
        it.tag = tag;
        it.target = self;
        if (tag == maxLinesTagForValue(_currentMaxLines)) it.state = NSControlStateValueOn;
        [maxLinesSub addItem:it];
    }
    maxLinesItem.submenu = maxLinesSub;
    [menu addItem:maxLinesItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // --- 偏移调整 ---
    NSMenuItem *offsetItem = [[NSMenuItem alloc] initWithTitle:@"偏移调整"
                                                        action:nil
                                                 keyEquivalent:@""];
    NSMenu *offsetSub = [[NSMenu alloc] initWithTitle:@""];
    NSMenuItem *op1 = [[NSMenuItem alloc] initWithTitle:@"+1.0s (延迟)"
                                                 action:@selector(handleMenuItem:)
                                          keyEquivalent:@""];
    op1.tag = DeskMenuTagOffsetPlus1000; op1.target = self;
    NSMenuItem *op2 = [[NSMenuItem alloc] initWithTitle:@"+0.5s"
                                                 action:@selector(handleMenuItem:)
                                          keyEquivalent:@""];
    op2.tag = DeskMenuTagOffsetPlus500; op2.target = self;
    NSMenuItem *op3 = [[NSMenuItem alloc] initWithTitle:@"重置为 0"
                                                 action:@selector(handleMenuItem:)
                                          keyEquivalent:@""];
    op3.tag = DeskMenuTagOffsetReset; op3.target = self;
    NSMenuItem *op4 = [[NSMenuItem alloc] initWithTitle:@"-0.5s"
                                                 action:@selector(handleMenuItem:)
                                          keyEquivalent:@""];
    op4.tag = DeskMenuTagOffsetMinus500; op4.target = self;
    NSMenuItem *op5 = [[NSMenuItem alloc] initWithTitle:@"-1.0s (提前)"
                                                 action:@selector(handleMenuItem:)
                                          keyEquivalent:@""];
    op5.tag = DeskMenuTagOffsetMinus1000; op5.target = self;
    [offsetSub addItem:op1];
    [offsetSub addItem:op2];
    [offsetSub addItem:op3];
    [offsetSub addItem:op4];
    [offsetSub addItem:op5];
    offsetItem.submenu = offsetSub;
    [menu addItem:offsetItem];

    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *prefItem = [[NSMenuItem alloc] initWithTitle:@"设置..."
                                                       action:@selector(handleMenuItem:)
                                                keyEquivalent:@""];
    prefItem.tag = DeskMenuTagPreferences;
    prefItem.target = self;
    [menu addItem:prefItem];

    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *quitDeskItem = [[NSMenuItem alloc] initWithTitle:@"退出桌面歌词"
                                                          action:@selector(handleMenuItem:)
                                                   keyEquivalent:@""];
    quitDeskItem.tag = DeskMenuTagQuitDesktopLyrics;
    quitDeskItem.target = self;
    [menu addItem:quitDeskItem];

    NSMenuItem *quitFoobarItem = [[NSMenuItem alloc] initWithTitle:@"退出 foobar"
                                                            action:@selector(handleMenuItem:)
                                                     keyEquivalent:@""];
    quitFoobarItem.tag = DeskMenuTagQuitFoobar;
    quitFoobarItem.target = self;
    [menu addItem:quitFoobarItem];

    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    [menu popUpMenuPositioningItem:nil atLocation:loc inView:self];
}

- (void)handleMenuItem:(NSMenuItem *)sender {
    DeskMenuTag tag = (DeskMenuTag)sender.tag;
    switch (tag) {
        case DeskMenuTagReSearchLrcLib:
            if (_onReSearchSource) _onReSearchSource(@"lrclib");
            break;
        case DeskMenuTagReSearchNetEase:
            if (_onReSearchSource) _onReSearchSource(@"netease");
            break;
        case DeskMenuTagReSearchQQ:
            if (_onReSearchSource) _onReSearchSource(@"qqmusic");
            break;
        case DeskMenuTagDeleteLyric:
            if (_onDeleteLyric) _onDeleteLyric();
            break;
        case DeskMenuTagMaxLines3:
        case DeskMenuTagMaxLines4:
        case DeskMenuTagMaxLines5:
        case DeskMenuTagMaxLines6:
        case DeskMenuTagMaxLines7:
            if (_onSetMaxLines) _onSetMaxLines(maxLinesValueForTag(tag));
            break;
        case DeskMenuTagOffsetPlus1000:
            if (_onAdjustOffset) _onAdjustOffset(1000);
            break;
        case DeskMenuTagOffsetPlus500:
            if (_onAdjustOffset) _onAdjustOffset(500);
            break;
        case DeskMenuTagOffsetReset:
            if (_onResetOffset) _onResetOffset();
            break;
        case DeskMenuTagOffsetMinus500:
            if (_onAdjustOffset) _onAdjustOffset(-500);
            break;
        case DeskMenuTagOffsetMinus1000:
            if (_onAdjustOffset) _onAdjustOffset(-1000);
            break;
        case DeskMenuTagPlayPause:
            if (_onPlayPause) _onPlayPause();
            break;
        case DeskMenuTagStop:
            if (_onStop) _onStop();
            break;
        case DeskMenuTagPrevious:
            if (_onPrevious) _onPrevious();
            break;
        case DeskMenuTagNext:
            if (_onNext) _onNext();
            break;
        case DeskMenuTagPreferences:
            if (_onOpenPreferences) _onOpenPreferences();
            break;
        case DeskMenuTagQuitDesktopLyrics:
            if (_onQuitDesktopLyrics) _onQuitDesktopLyrics();
            break;
        case DeskMenuTagQuitFoobar:
            if (_onQuitFoobar) _onQuitFoobar();
            break;
    }
}

@end

@interface DesktopLyricsController () <NSWindowDelegate>
@end

@implementation DesktopLyricsController {
    NSPanel *_panel;
    LyricView *_lyricView;
    DeskLyricsContent *_contentView;

    openlyrics::LyricData _currentLyricData;
    int64_t _currentExtraOffsetMs;
    int64_t _trackRequestToken;
    std::string _currentLyricPath;   // 当前歌词来源的本地文件绝对路径；内嵌标签/未找到时为空

    BOOL _tapActive;                              // 是否处于打轴模式
    int _tapAnchorLine;                           // 当前手动高亮行（data.lines 下标，-1 合法）
    std::vector<std::pair<int,int64_t>> _tapUndo; // 每次前进前保存 (行下标, 原 timeMs)，供上键撤销

    int _lrclibFailures;
    int _neteaseFailures;
    int _qqmusicFailures;

    openlyrics::AppConfig _config;
    NSTimer *_syncTimer;
    BOOL _started;
    BOOL _appIsActive;
    BOOL _savePending;
    BOOL _suppressSelfLyricReload;   // 抑制本控制器自身发起的歌词变更通知触发的重载，避免与异步保存竞态覆盖内存歌词
    std::shared_ptr<openlyrics::CancelToken> _cancelToken;
}

+ (instancetype)sharedController {
    static DesktopLyricsController *instance = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        instance = [[DesktopLyricsController alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self == nil) return nil;
    _started = NO;
    _appIsActive = [NSApp isActive];
    _trackRequestToken = 0;
    return self;
}

- (void)start {
    if (_started) return;
    _config = openlyrics::ConfigAdapter().load();

    if (!_config.deskLyrics.enabled) {
        _started = YES;
        return;
    }

    [self setupPanel];

    [[PlaybackHub sharedHub] addObserver:self];

    [[NSNotificationCenter defaultCenter] addObserver:self
        selector:@selector(appDidResignActive:)
        name:NSApplicationDidResignActiveNotification object:NSApp];
    [[NSNotificationCenter defaultCenter] addObserver:self
        selector:@selector(appDidBecomeActive:)
        name:NSApplicationDidBecomeActiveNotification object:NSApp];

    _started = YES;

    if (!_appIsActive || !_config.deskLyrics.showOnlyInBackground) {
        [self showPanel];
    }
    [self handleTrackChanged];
}

- (void)stop {
    if (!_started) return;
    if (_cancelToken) _cancelToken->cancel();
    [self hidePanel];
    [[PlaybackHub sharedHub] removeObserver:self];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _started = NO;
}

- (void)dealloc {
    [self stop];
}

#pragma mark - NSPanel

- (NSRect)clampFrameToVisible:(NSRect)frame {
    NSScreen *screen = _panel.screen ?: [NSScreen screens].firstObject;
    if (screen == nil) return frame;
    NSRect s = screen.visibleFrame;
    CGFloat minVisible = 40.0;
    if (frame.origin.x + frame.size.width < s.origin.x + minVisible)
        frame.origin.x = s.origin.x + minVisible - frame.size.width;
    if (frame.origin.x > s.origin.x + s.size.width - minVisible)
        frame.origin.x = s.origin.x + s.size.width - minVisible;
    if (frame.origin.y + frame.size.height < s.origin.y + minVisible)
        frame.origin.y = s.origin.y + minVisible - frame.size.height;
    if (frame.origin.y > s.origin.y + s.size.height - minVisible)
        frame.origin.y = s.origin.y + s.size.height - minVisible;
    return frame;
}

- (void)setupPanel {
    const auto& d = _config.deskLyrics;
    NSScreen *screen = [NSScreen screens].firstObject;
    NSRect screenFrame = screen.frame;

    CGFloat panelW = d.windowWidth;
    CGFloat panelH = d.windowHeight;
    CGFloat panelX, panelY;
    if (d.windowX >= 0 && d.windowY >= 0) {
        panelX = d.windowX;
        panelY = d.windowY;
    } else {
        panelX = (screenFrame.size.width - panelW) / 2.0;
        panelY = screenFrame.size.height * 0.75;
    }

    NSRect initialFrame = NSMakeRect(panelX, panelY, panelW, panelH);

    _panel = [[DeskLyricsPanel alloc] initWithContentRect:initialFrame
        styleMask:NSWindowStyleMaskBorderless
              | NSWindowStyleMaskNonactivatingPanel
              | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    _panel.level = NSFloatingWindowLevel;
    _panel.backgroundColor = NSColor.clearColor;
    _panel.opaque = NO;
    _panel.hasShadow = YES;  // 圆角毛玻璃卡片投柔和阴影，强化悬浮层次
    _panel.movableByWindowBackground = NO;
    _panel.hidesOnDeactivate = NO;
    _panel.releasedWhenClosed = NO;
    _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
                              | NSWindowCollectionBehaviorStationary
                              | NSWindowCollectionBehaviorIgnoresCycle;
    _panel.minSize = NSMakeSize(kMinPanelWidth, kMinPanelHeight);
    _panel.delegate = self;

    [[NSNotificationCenter defaultCenter] addObserver:self
        selector:@selector(windowDidMove:)
        name:NSWindowDidMoveNotification object:_panel];

    _contentView = [[DeskLyricsContent alloc] initWithFrame:_panel.contentView.bounds];
    _contentView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _contentView.currentMaxLines = d.maxLines;

    __weak __typeof__(self) weakSelf = self;
    _contentView.onMoveTo = ^(NSPoint origin) {
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        NSRect frame = strongSelf->_panel.frame;
        frame.origin = origin;
        frame = [strongSelf clampFrameToVisible:frame];
        [strongSelf->_panel setFrameOrigin:frame.origin];
        [strongSelf schedulePositionSave];
    };

    _contentView.onResizeFrame = ^(NSRect frame) {
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        NSRect clamped = [strongSelf clampFrameToVisible:frame];
        [strongSelf->_panel setFrame:clamped display:YES];
        [strongSelf->_lyricView invalidateRowHeights];
    };

    _contentView.onResizeCommit = ^{
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        [strongSelf saveSizeAndPosition];
        [strongSelf->_lyricView invalidateRowHeights];
    };

    _contentView.onOffsetDelta = ^(int64_t deltaMs) {
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        int64_t total = strongSelf->_currentLyricData.offsetMs + deltaMs;
        openlyrics::SyncResult result = openlyrics::SyncEngine::locate(
            strongSelf->_currentLyricData, [strongSelf currentPositionMs], total);
        [strongSelf->_lyricView setSyncResult:result];
    };

    _contentView.onOffsetCommit = ^(int64_t totalDeltaMs) {
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        [strongSelf commitOffsetDelta:totalDeltaMs];
    };

    _contentView.onTapAnchor = ^{
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        [strongSelf tapAnchorAtCurrent];
    };
    _contentView.onTapStep = ^(int dir) {
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        [strongSelf tapStep:dir];
    };

    // 右键菜单回调
    _contentView.onReSearchSource = ^(NSString *sourceKey) {
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        [strongSelf reSearchFromSource:sourceKey];
    };

    _contentView.onDeleteLyric = ^{
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        [strongSelf deleteCurrentLyricFile];
    };

    _contentView.onSetMaxLines = ^(NSInteger lines) {
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        strongSelf->_config.deskLyrics.maxLines = (int)lines;
        openlyrics::ConfigAdapter().save(strongSelf->_config);
        strongSelf->_lyricView.maxLines = lines;
        strongSelf->_contentView.currentMaxLines = lines;
    };

    _contentView.onAdjustOffset = ^(int64_t deltaMs) {
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        [strongSelf commitOffsetDelta:deltaMs];
    };

    _contentView.onResetOffset = ^{
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        [strongSelf commitOffsetDelta:-(strongSelf->_currentLyricData.offsetMs)];
    };

    _contentView.onPlayPause = ^{
        auto pc = playback_control::get();
        if (pc.is_empty()) return;
        if (pc->is_playing() && !pc->is_paused()) pc->pause(true);
        else if (pc->is_paused()) pc->pause(false);
        else pc->start(playback_control::track_command_play);
    };

    _contentView.onStop = ^{
        auto pc = playback_control::get();
        if (!pc.is_empty()) pc->stop();
    };

    _contentView.onPrevious = ^{
        auto pc = playback_control::get();
        if (!pc.is_empty()) pc->start(playback_control::track_command_prev);
    };

    _contentView.onNext = ^{
        auto pc = playback_control::get();
        if (!pc.is_empty()) pc->start(playback_control::track_command_next);
    };

    _contentView.onOpenPreferences = ^{
        auto uic = ui_control::get();
        if (!uic.is_empty()) {
            uic->show_preferences(g_guid_openlyrics_preferences);
        }
    };

    _contentView.onQuitDesktopLyrics = ^{
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        strongSelf->_config.deskLyrics.enabled = false;
        openlyrics::ConfigAdapter().save(strongSelf->_config);
        [strongSelf updateVisibility];
    };

    _contentView.onQuitFoobar = ^{
        [NSApp terminate:nil];
    };

    // 内缩 kContentInset，让歌词文字避开圆角与描边，四周留出均匀留白。
    _lyricView = [[LyricView alloc] initWithFrame:NSInsetRect(_contentView.bounds, kContentInset, kContentInset)];
    _lyricView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _lyricView.transparentBackground = YES;
    [_contentView addSubview:_lyricView];

    _panel.contentView = _contentView;
    [self applyDeskLyricsDisplay];
}

- (void)applyDeskLyricsDisplay {
    const auto& d = _config.deskLyrics;
    openlyrics::DisplayConfig dc;
    dc.fontName = _config.display.fontName;
    dc.fontSize = d.fontSize;
    dc.highlightScale = _config.display.highlightScale;
    dc.normalColor = d.normalColor;
    dc.highlightColor = d.highlightColor;
    dc.titleColor = d.titleColor;
    dc.alignment = d.alignment;
    dc.lineSpacing = d.lineSpacing;
    [_lyricView applyDisplayConfig:dc];
    _lyricView.maxLines = d.maxLines;
    _contentView.currentMaxLines = d.maxLines;
    [self updateTitle];
}

// 依据 showTitle 与当前曲目，拼装并下发「歌名 — 艺术家」标题。
- (NSString *)titleStringForCurrentTrack {
    if (!_config.deskLyrics.showTitle) return nil;
    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) return nil;
    openlyrics::TrackMeta meta = [hub currentTrack];
    NSString *title = [NSString stringWithUTF8String:meta.title.c_str()] ?: @"";
    NSString *artist = [NSString stringWithUTF8String:meta.artist.c_str()] ?: @"";
    if (title.length > 0 && artist.length > 0)
        return [NSString stringWithFormat:@"%@ — %@", title, artist];
    if (title.length > 0) return title;
    if (artist.length > 0) return artist;
    return nil;
}

- (void)updateTitle {
    [_lyricView setTitleText:[self titleStringForCurrentTrack]];
}

#pragma mark - 窗口位置/尺寸持久化

- (void)schedulePositionSave {
    if (_savePending) return;
    _savePending = YES;
    __weak __typeof__(self) weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(kSaveDebounce * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        strongSelf->_savePending = NO;
        [strongSelf saveSizeAndPosition];
    });
}

- (void)saveSizeAndPosition {
    NSRect frame = _panel.frame;
    _config.deskLyrics.windowWidth = frame.size.width;
    _config.deskLyrics.windowHeight = frame.size.height;
    _config.deskLyrics.windowX = frame.origin.x;
    _config.deskLyrics.windowY = frame.origin.y;
    openlyrics::ConfigAdapter().save(_config);
}

- (void)windowDidResize:(NSNotification *)notification {
    (void)notification;
    [self schedulePositionSave];
    [_lyricView invalidateRowHeights];
}

- (void)windowDidMove:(NSNotification *)notification {
    (void)notification;
    [self schedulePositionSave];
}

#pragma mark - 可见性

- (void)showPanel {
    if (_panel == nil) return;
    [_panel orderFront:nil];
    if (_syncTimer == nil) {
        __weak __typeof__(self) weakSelf = self;
        _syncTimer = [NSTimer scheduledTimerWithTimeInterval:kSyncTickInterval repeats:YES
                                                       block:^(NSTimer *timer) {
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) { [timer invalidate]; return; }
            [strongSelf tickSync];
        }];
    }
}

- (void)hidePanel {
    [_syncTimer invalidate];
    _syncTimer = nil;
    [_lyricView stopAnimation];
    [_panel orderOut:nil];
}

- (void)updateVisibility {
    if (!_config.deskLyrics.enabled) {
        [self hidePanel];
        return;
    }
    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) {
        if (_config.deskLyrics.showOnlyInBackground) [self hidePanel];
        return;
    }
    if (_config.deskLyrics.showOnlyInBackground && _appIsActive) {
        [self hidePanel];
    } else {
        [self showPanel];
    }
}

- (void)reloadConfig {
    _config = openlyrics::ConfigAdapter().load();
    if (_panel == nil && _config.deskLyrics.enabled) {
        [self setupPanel];
    } else if (_panel != nil) {
        const auto& d = _config.deskLyrics;
        NSRect curFrame = _panel.frame;
        if (fabs(curFrame.size.width - d.windowWidth) > 0.5 ||
            fabs(curFrame.size.height - d.windowHeight) > 0.5) {
            NSRect newFrame = curFrame;
            newFrame.size.width = d.windowWidth;
            newFrame.size.height = d.windowHeight;
            [_panel setFrame:newFrame display:YES];
            _config.deskLyrics.windowWidth = newFrame.size.width;
            _config.deskLyrics.windowHeight = newFrame.size.height;
            openlyrics::ConfigAdapter().save(_config);
        }
    }
    [self applyDeskLyricsDisplay];
    [self updateVisibility];
    if (_config.deskLyrics.enabled) [self handleTrackChanged];
}

#pragma mark - NSApplication 通知

- (void)appDidResignActive:(NSNotification *)note {
    (void)note;
    _appIsActive = NO;
    [self updateVisibility];
}

- (void)appDidBecomeActive:(NSNotification *)note {
    (void)note;
    _appIsActive = YES;
    [self updateVisibility];
}

#pragma mark - PlaybackHubObserving

- (void)playbackHubDidChange {
    [self exitTapSync];
    [self handleTrackChanged];
}

- (void)playbackHubLyricDidChange {
    // 主面板偏移/歌词变更后从本地文件重新加载。
    // 本控制器自身提交偏移/删除时也会广播，此处须跳过，否则会触发从磁盘重载，
    // 与自身的异步 forceSave 竞态，把已更新的内存歌词覆盖成空 → 显示"无歌词"。
    if (_suppressSelfLyricReload) return;
    if (!_started || !_config.deskLyrics.enabled) return;
    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) return;

    openlyrics::TrackMeta meta = [hub currentTrack];
    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        openlyrics::TagIOAdapter tagAdapter;
        openlyrics::FileSystemAdapter fsAdapter;
        openlyrics::TagSource tagSource(tagAdapter);
        openlyrics::LocalFileSource localSource(fsAdapter);
        openlyrics::SearchPipeline localPipeline({&tagSource, &localSource});

        openlyrics::LyricData resolved;
        bool found = localPipeline.resolve(meta, resolved);
        std::string lyricPath;
        if (found) {
            openlyrics::LyricData tagProbe;
            if (tagSource.fetch(meta, tagProbe)) {
                // tag 命中，无文件路径
            } else if (localSource.resolvePath(meta, lyricPath)) {
                // 本地文件命中
            }
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) return;
            strongSelf->_currentLyricData = found ? resolved : openlyrics::LyricData{};
            strongSelf->_currentExtraOffsetMs = strongSelf->_config.defaultOffsetMs;
            strongSelf->_currentLyricPath = lyricPath;
            strongSelf->_contentView.canDeleteLyric = !lyricPath.empty();
            [strongSelf->_lyricView setLyricData:strongSelf->_currentLyricData];
        });
    });
}

#pragma mark - 曲目切换

- (void)handleTrackChanged {
    if (!_started || !_config.deskLyrics.enabled) return;
    if (_cancelToken) _cancelToken->cancel();
    _cancelToken = std::make_shared<openlyrics::CancelToken>();
    auto cancel = _cancelToken;

    _trackRequestToken += 1;
    const int64_t requestToken = _trackRequestToken;

    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) {
        _currentLyricData = openlyrics::LyricData{};
        _currentExtraOffsetMs = 0;
        [_lyricView setPlaceholderText:@"停止播放"];
        [_lyricView setLyricData:_currentLyricData];
        if (_panel != nil) {
            _panel.alphaValue = 0.25;
        }
        [self updateTitle];
        [self updateVisibility];
        return;
    }

    if (_panel != nil) {
        _panel.alphaValue = 1.0;
    }
    [_lyricView setPlaceholderText:@"无歌词"];

    [self updateVisibility];
    [self updateTitle];

    openlyrics::TrackMeta meta = [hub currentTrack];
    openlyrics::AppConfig config = _config;
    int maxFail = config.maxConsecutiveFailures;

    __block int lrclibFails = _lrclibFailures;
    __block int neteaseFails = _neteaseFailures;
    __block int qqmusicFails = _qqmusicFailures;

    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        if (cancel->isCancelled()) return;

        openlyrics::TagIOAdapter tagAdapter;
        openlyrics::FileSystemAdapter fsAdapter;
        openlyrics::TagSource tagSource(tagAdapter);
        openlyrics::LocalFileSource localSource(fsAdapter);
        openlyrics::SearchPipeline localPipeline({&tagSource, &localSource});

        // 构建在线源列表
        std::vector<openlyrics::LyricSource*> onlineSources;
        openlyrics::HttpAdapter http;
        openlyrics::CryptoAdapter crypto;
        openlyrics::LrcLibProvider lrcLib(http);
        openlyrics::NetEaseProvider netease(http, crypto);
        openlyrics::QQMusicProvider qqmusic(http, crypto);

        for (const auto& src : config.sources) {
            if (!src.enabled) continue;
            if (src.key == "lrclib") onlineSources.push_back(&lrcLib);
            else if (src.key == "netease") onlineSources.push_back(&netease);
            else if (src.key == "qqmusic") onlineSources.push_back(&qqmusic);
        }

        openlyrics::Matcher matcher;
        openlyrics::SearchCoordinator coordinator(&localPipeline, onlineSources, matcher);

        openlyrics::LyricData resolved;
        bool found = coordinator.resolve(meta, resolved, cancel.get());
        if (cancel->isCancelled()) return;
        std::string lyricPath;
        if (found) {
            // 反查匹配源以确定 lyricPath
            openlyrics::LyricData tagProbe;
            if (tagSource.fetch(meta, tagProbe)) {
                // 内嵌标签命中，无独立文件可删
            } else if (localSource.resolvePath(meta, lyricPath)) {
                // 本地文件命中
            } else {
                // 在线命中 → 落盘
                openlyrics::LyricStore store(fsAdapter);
                if (store.save(meta, resolved)) {
                    lyricPath = openlyrics::LocalFileSource::stripExtension(meta.path) + ".lrc";
                }
            }
        }

        // 更新失效计数
        if (found) {
            lrclibFails = 0;
            neteaseFails = 0;
            qqmusicFails = 0;
        } else {
            if (lrclibFails < maxFail) lrclibFails++;
            if (neteaseFails < maxFail) neteaseFails++;
            if (qqmusicFails < maxFail) qqmusicFails++;
        }

        if (!found) resolved = openlyrics::LyricData{};

        dispatch_async(dispatch_get_main_queue(), ^{
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) return;
            if (strongSelf->_trackRequestToken != requestToken) return;
            if (cancel->isCancelled()) return;

            strongSelf->_lrclibFailures = lrclibFails;
            strongSelf->_neteaseFailures = neteaseFails;
            strongSelf->_qqmusicFailures = qqmusicFails;
            strongSelf->_currentLyricData = resolved;
            strongSelf->_currentExtraOffsetMs = config.defaultOffsetMs;
            strongSelf->_currentLyricPath = lyricPath;
            strongSelf->_contentView.canDeleteLyric = !lyricPath.empty();
            [strongSelf->_lyricView setLyricData:resolved];
        });
    });
}

#pragma mark - 按源重新搜索 / 删除歌词文件

// 用户从右键菜单指定某一在线源重新搜索：忽略 enabled 与失败计数（显式操作优先），
// 命中则覆盖写入同名 .lrc 并刷新显示；未命中弹瞬时 HUD 提示，保留原有歌词。
- (void)reSearchFromSource:(NSString *)sourceKey {
    if (!_started || !_config.deskLyrics.enabled) return;
    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) return;

    if (_cancelToken) _cancelToken->cancel();
    _cancelToken = std::make_shared<openlyrics::CancelToken>();
    auto cancel = _cancelToken;

    _trackRequestToken += 1;
    const int64_t requestToken = _trackRequestToken;
    openlyrics::TrackMeta meta = [hub currentTrack];
    std::string key = sourceKey.UTF8String;

    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        if (cancel->isCancelled()) return;

        openlyrics::HttpAdapter http;
        openlyrics::CryptoAdapter crypto;
        openlyrics::LrcLibProvider lrcLib(http);
        openlyrics::NetEaseProvider netease(http, crypto);
        openlyrics::QQMusicProvider qqmusic(http, crypto);

        std::vector<openlyrics::LyricSource*> onlineSources;
        if (key == "lrclib") onlineSources.push_back(&lrcLib);
        else if (key == "netease") onlineSources.push_back(&netease);
        else if (key == "qqmusic") onlineSources.push_back(&qqmusic);

        openlyrics::LyricData data;
        bool found = false;
        if (!onlineSources.empty()) {
            openlyrics::Matcher matcher;
            openlyrics::SearchCoordinator coordinator(onlineSources, matcher);
            found = coordinator.resolve(meta, data, cancel.get());
        }
        if (cancel->isCancelled()) return;

        std::string savedPath;
        if (found) {
            openlyrics::FileSystemAdapter fsAdapter;
            openlyrics::LyricStore store(fsAdapter);
            // 覆盖写入音频同目录同名 .lrc，替换掉可能存在的错误歌词。
            if (store.forceSave(meta, data)) {
                savedPath = openlyrics::LocalFileSource::stripExtension(meta.path) + ".lrc";
            }
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) return;
            if (strongSelf->_trackRequestToken != requestToken) return;
            if (cancel->isCancelled()) return;
            if (found) {
                strongSelf->_currentLyricData = data;
                strongSelf->_currentExtraOffsetMs = strongSelf->_config.defaultOffsetMs;
                strongSelf->_currentLyricPath = savedPath;
                strongSelf->_contentView.canDeleteLyric = !savedPath.empty();
                [strongSelf->_lyricView setLyricData:data];
                // 通知主面板同步新歌词
                [[PlaybackHub sharedHub] notifyLyricChanged];
            } else {
                [strongSelf->_contentView showMessage:@"未找到歌词"];
            }
        });
    });
}

// 删除当前歌词来源文件（用户判断显示错误时）。按既定设计不弹确认，删除后清空显示，
// 等待用户手动重新搜索。删除后重置失败计数，使下次重搜不被计数门槛拦截。
- (void)deleteCurrentLyricFile {
    if (_currentLyricPath.empty()) return;

    openlyrics::FileSystemAdapter fsAdapter;
    bool ok = fsAdapter.removeFile(_currentLyricPath);

    _currentLyricPath.clear();
    _currentLyricData = openlyrics::LyricData{};
    _currentExtraOffsetMs = 0;
    _lrclibFailures = 0;
    _neteaseFailures = 0;
    _qqmusicFailures = 0;
    _contentView.canDeleteLyric = NO;
    [_lyricView setLyricData:_currentLyricData];
    [_contentView showMessage:ok ? @"已删除歌词文件" : @"删除失败"];

    // 通知主面板同步删除
    [self broadcastLyricChangedFromSelf];
}

#pragma mark - 同步 tick

- (void)tickSync {
    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) return;

    if (_tapActive) {                       // 打轴中：冻结在手动锚点，不随播放定位
        [self freezeHighlightTo:_tapAnchorLine];
        return;
    }

    int64_t posMs = [self currentPositionMs];
    if (posMs < 0) posMs = 0;

    openlyrics::SyncResult result = openlyrics::SyncEngine::locate(
        _currentLyricData, posMs, _currentExtraOffsetMs);
    [_lyricView setSyncResult:result];
}

- (int64_t)currentPositionMs {
    auto pc = playback_control::get();
    if (!pc.is_empty()) {
        return static_cast<int64_t>(pc->playback_get_position() * 1000.0 + 0.5);
    }
    return [[PlaybackHub sharedHub] positionMs];
}

#pragma mark - 逐句手动打轴

// 冻结桌面高亮到指定行（progress=0），避免下一次 tickSync 用播放位置覆盖。
- (void)freezeHighlightTo:(int)line {
    openlyrics::SyncResult r;
    r.lineIndex = line;
    r.progress = 0.0;
    [_lyricView setSyncResult:r];
}

// 右键：锁定当前高亮句为锚点，进入打轴模式。
- (void)tapAnchorAtCurrent {
    if (_currentLyricData.lines.empty()) return;
    _tapActive = YES;
    openlyrics::SyncResult cur = openlyrics::SyncEngine::locate(
        _currentLyricData, [self currentPositionMs], _currentExtraOffsetMs);
    _tapAnchorLine = cur.lineIndex;
    _tapUndo.clear();
    [self freezeHighlightTo:_tapAnchorLine];
    [[PlaybackHub sharedHub] notifyManualHighlightLine:_tapAnchorLine];
}

// 上下键/滚轮一步：dir>0 前进并写时标，dir<0 回退撤销。
- (void)tapStep:(int)dir {
    if (!_tapActive || _currentLyricData.lines.empty()) return;

    if (dir > 0) {
        int next = openlyrics::SyncEngine::adjacentTimedLine(_currentLyricData, _tapAnchorLine, 1);
        if (next == _tapAnchorLine || next < 0) return;   // 末句停住 / 无时标行
        _tapUndo.push_back({next, _currentLyricData.lines[next].timeMs});
        _currentLyricData.lines[next].timeMs = [self currentPositionMs];
        _tapAnchorLine = next;
    } else {
        // 撤销当前锚点行本次打轴（若栈顶正是它）
        if (!_tapUndo.empty() && _tapUndo.back().first == _tapAnchorLine) {
            _currentLyricData.lines[_tapAnchorLine].timeMs = _tapUndo.back().second;
            _tapUndo.pop_back();
        }
        int prev = openlyrics::SyncEngine::adjacentTimedLine(_currentLyricData, _tapAnchorLine, -1);
        if (prev == _tapAnchorLine) {                     // 首句停住：仅可能已撤销，无行移动
            [self persistTapAndBroadcast];
            return;
        }
        _tapAnchorLine = prev;
    }
    [self persistTapAndBroadcast];
}

// 重建 sourceText、写盘、刷新桌面高亮、广播主面板（数据 + 手动高亮）。
- (void)persistTapAndBroadcast {
    _currentLyricData.sourceText = openlyrics::LrcSerializer::serialize(_currentLyricData);
    _currentLyricData.synced = true;

    openlyrics::TrackMeta meta = [[PlaybackHub sharedHub] currentTrack];
    openlyrics::FileSystemAdapter fsAdapter;
    openlyrics::LyricStore store(fsAdapter);
    if (store.forceSave(meta, _currentLyricData)) {
        _currentLyricPath = openlyrics::LocalFileSource::stripExtension(meta.path) + ".lrc";
        _contentView.canDeleteLyric = YES;
    }

    [_lyricView setLyricData:_currentLyricData];
    [self freezeHighlightTo:_tapAnchorLine];
    [self broadcastLyricChangedFromSelf];                       // 主面板重载新时标
    [[PlaybackHub sharedHub] notifyManualHighlightLine:_tapAnchorLine];  // 主面板高亮同一句
}

// 退出打轴，恢复自动跟随。
- (void)exitTapSync {
    if (!_tapActive) return;
    _tapActive = NO;
    _tapUndo.clear();
    [[PlaybackHub sharedHub] notifyManualHighlightLine:-1];
}

#pragma mark - 偏移拖拽提交

// 广播歌词/偏移变更给其他观察者（主面板），并抑制对本控制器自身的回调，
// 避免自触发从磁盘重载与异步保存竞态覆盖内存歌词。notifyLyricChanged 为同步同线程回调，
// 故标志位包裹即可精确跳过自身回调，不影响主面板等其他观察者。
- (void)broadcastLyricChangedFromSelf {
    _suppressSelfLyricReload = YES;
    [[PlaybackHub sharedHub] notifyLyricChanged];
    _suppressSelfLyricReload = NO;
}

- (void)commitOffsetDelta:(int64_t)deltaMs {
    if (_currentLyricData.lines.empty()) return;

    int64_t newOffset = _currentLyricData.offsetMs + deltaMs;

    std::string updatedText = _currentLyricData.sourceText;
    const std::string needle = "\n[offset:";
    size_t offsetPos = updatedText.find(needle);
    if (offsetPos != std::string::npos) {
        size_t tagStart = offsetPos + 1;
        size_t closeBracket = updatedText.find(']', tagStart);
        if (closeBracket != std::string::npos) {
            updatedText.replace(tagStart, closeBracket - tagStart + 1,
                                "[offset:" + std::to_string(newOffset) + "]");
        }
    } else if (updatedText.compare(0, 8, "[offset:") == 0) {
        size_t closeBracket = updatedText.find(']');
        if (closeBracket != std::string::npos) {
            updatedText.replace(0, closeBracket + 1,
                                "[offset:" + std::to_string(newOffset) + "]");
        }
    } else {
        updatedText = "[offset:" + std::to_string(newOffset) + "]\n" + updatedText;
    }

    _currentLyricData = openlyrics::LrcParser::parse(updatedText);
    [_lyricView setLyricData:_currentLyricData];

    PlaybackHub *hub = [PlaybackHub sharedHub];
    if ([hub hasTrack]) {
        openlyrics::TrackMeta meta = [hub currentTrack];
        openlyrics::LyricData dataCopy = _currentLyricData;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            openlyrics::FileSystemAdapter fs;
            openlyrics::LyricStore store(fs);
            store.forceSave(meta, dataCopy);
        });
    }

    // 通知主面板同步偏移
    [self broadcastLyricChangedFromSelf];
}

#pragma mark - NSWindowDelegate

- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
}

- (void)windowDidResignKey:(NSNotification *)notification {
    (void)notification;
    [self exitTapSync];
}

@end
