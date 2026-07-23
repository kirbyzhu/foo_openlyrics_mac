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

#include "sources/TagSource.h"
#include "sources/LocalFileSource.h"
#include "sources/LrcLibProvider.h"
#include "sources/NetEaseProvider.h"
#include "sources/QQMusicProvider.h"
#include "pipeline/SearchPipeline.h"
#include "store/LyricStore.h"
#include "sync/SyncEngine.h"
#include "model/LyricData.h"
#include "config/AppConfig.h"

static const NSTimeInterval kSyncTickInterval = 0.06;

// 支持 mouseDownCanMoveWindow 的容器视图，使 NSPanel 整个内容区可拖拽移动。
@interface DeskLyricsContent : NSView
@end
@implementation DeskLyricsContent
- (BOOL)mouseDownCanMoveWindow { return YES; }
@end

@implementation DesktopLyricsController {
    NSPanel *_panel;
    LyricView *_lyricView;

    openlyrics::LyricData _currentLyricData;
    int64_t _currentExtraOffsetMs;
    int64_t _trackRequestToken;
    NSInteger _resolveSerial;

    int _lrclibFailures;
    int _neteaseFailures;
    int _qqmusicFailures;

    openlyrics::AppConfig _config;
    NSTimer *_syncTimer;
    BOOL _started;
    BOOL _appIsActive;
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
    _resolveSerial = 0;
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

    // 根据当前前后台状态决定初始可见性
    if (!_appIsActive || !_config.deskLyrics.showOnlyInBackground) {
        [self showPanel];
    }
    [self handleTrackChanged];
}

- (void)stop {
    if (!_started) return;
    [self hidePanel];
    [[PlaybackHub sharedHub] removeObserver:self];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _started = NO;
}

- (void)dealloc {
    [self stop];
}

#pragma mark - NSPanel

- (void)setupPanel {
    NSRect screenFrame = [NSScreen screens].firstObject.frame;
    CGFloat panelW = 600;
    CGFloat panelH = 120;
    CGFloat panelX = (screenFrame.size.width - panelW) / 2.0;
    CGFloat panelY = screenFrame.size.height * 0.75;

    _panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(panelX, panelY, panelW, panelH)
        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
        backing:NSBackingStoreBuffered defer:NO];
    _panel.level = NSFloatingWindowLevel;
    _panel.backgroundColor = NSColor.clearColor;
    _panel.opaque = NO;
    _panel.hasShadow = NO;
    _panel.movableByWindowBackground = YES;
    _panel.hidesOnDeactivate = NO;
    _panel.releasedWhenClosed = NO;
    _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
                              | NSWindowCollectionBehaviorStationary
                              | NSWindowCollectionBehaviorIgnoresCycle;
    DeskLyricsContent *content = [[DeskLyricsContent alloc] initWithFrame:_panel.contentView.bounds];
    content.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    _lyricView = [[LyricView alloc] initWithFrame:content.bounds];
    _lyricView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _lyricView.transparentBackground = YES;
    [content addSubview:_lyricView];

    _panel.contentView = content;
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
    dc.alignment = d.alignment;
    dc.lineSpacing = d.lineSpacing;
    [_lyricView applyDisplayConfig:dc];
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
    [self handleTrackChanged];
}

#pragma mark - 曲目切换：独立搜索管线

- (void)handleTrackChanged {
    if (!_started || !_config.deskLyrics.enabled) return;
    _trackRequestToken += 1;
    const int64_t requestToken = _trackRequestToken;

    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) {
        _currentLyricData = openlyrics::LyricData{};
        _currentExtraOffsetMs = 0;
        [_lyricView setLyricData:_currentLyricData];
        [self updateVisibility];
        return;
    }

    [self updateVisibility];

    openlyrics::TrackMeta meta = [hub currentTrack];
    openlyrics::AppConfig config = _config;
    int maxFail = config.maxConsecutiveFailures;

    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        openlyrics::TagIOAdapter tagAdapter;
        openlyrics::FileSystemAdapter fsAdapter;
        openlyrics::TagSource tagSource(tagAdapter);
        openlyrics::LocalFileSource localSource(fsAdapter);
        openlyrics::SearchPipeline pipeline({&tagSource, &localSource});

        openlyrics::LyricData resolved;
        bool found = pipeline.resolve(meta, resolved);

        if (!found) {
            bool onlineSaved = false;
            auto trySource = [&](const char* label, auto& provider,
                                 int& failures, int& counter) -> bool {
                if (counter >= maxFail) return false;
                openlyrics::LyricData data;
                if (provider.fetch(meta, data)) {
                    resolved = data; found = true; counter = 0;
                    openlyrics::LyricStore store(fsAdapter);
                    onlineSaved = store.save(meta, data);
                    return true;
                }
                ++counter;
                return false;
            };

            for (const auto& src : config.sources) {
                if (found) break;
                if (!src.enabled) continue;
                if (src.key == "tag" || src.key == "local") continue;

                if (src.key == "lrclib") {
                    openlyrics::HttpAdapter http;
                    openlyrics::LrcLibProvider lrcLib(http);
                    trySource("lrclib", lrcLib, _lrclibFailures, _lrclibFailures);
                } else if (src.key == "netease") {
                    openlyrics::HttpAdapter http;
                    openlyrics::CryptoAdapter crypto;
                    openlyrics::NetEaseProvider netease(http, crypto);
                    trySource("netease", netease, _neteaseFailures, _neteaseFailures);
                } else if (src.key == "qqmusic") {
                    openlyrics::HttpAdapter http;
                    openlyrics::CryptoAdapter crypto;
                    openlyrics::QQMusicProvider qqmusic(http, crypto);
                    trySource("qqmusic", qqmusic, _qqmusicFailures, _qqmusicFailures);
                }
            }
        }

        if (!found) resolved = openlyrics::LyricData{};

        dispatch_async(dispatch_get_main_queue(), ^{
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) return;
            if (strongSelf->_trackRequestToken != requestToken) return;

            strongSelf->_currentLyricData = resolved;
            strongSelf->_currentExtraOffsetMs = config.defaultOffsetMs;
            [strongSelf->_lyricView setLyricData:resolved];
        });
    });
}

#pragma mark - 同步 tick

- (void)tickSync {
    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) return;

    auto pc = playback_control::get();
    int64_t posMs = pc.is_empty() ? [hub positionMs]
        : static_cast<int64_t>(pc->playback_get_position() * 1000.0 + 0.5);
    if (posMs < 0) posMs = 0;

    openlyrics::SyncResult result = openlyrics::SyncEngine::locate(
        _currentLyricData, posMs, _currentExtraOffsetMs);
    [_lyricView setSyncResult:result];
}

@end
