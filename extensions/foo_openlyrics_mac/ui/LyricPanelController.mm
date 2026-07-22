// LyricPanelController.mm
// foo_openlyrics_mac —— Plan 2 Task 2：最小占位歌词面板控制器实现；
// Plan 2 Task 3：向 PlaybackHub 注册/注销，显示 "<title> — <mm:ss>"，
// 用 NSTimer(0.25s) 轮询 playback_control::get()->playback_get_position() 刷新位置。
#import "LyricPanelController.h"
#import "stdafx.h"

static NSString *const kPlaceholderText = @"未在播放";

@interface LyricPanelController ()
@property(nonatomic, strong) NSTextField *titleLabel;
@property(nonatomic, strong) NSTimer *positionTimer;
@end

@implementation LyricPanelController

- (void)loadView {
    // 用 NSBox（NSBoxCustom）取代 CALayer 背景填充，避免额外链接 QuartzCore。
    NSBox *root = [[NSBox alloc] initWithFrame:NSMakeRect(0, 0, 320, 200)];
    root.boxType = NSBoxCustom;
    root.borderType = NSNoBorder;
    root.fillColor = [NSColor controlBackgroundColor];

    NSTextField *label = [NSTextField labelWithString:kPlaceholderText];
    label.font = [NSFont systemFontOfSize:16];
    label.alignment = NSTextAlignmentCenter;
    label.lineBreakMode = NSLineBreakByTruncatingTail;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:label];

    [NSLayoutConstraint activateConstraints:@[
        [label.centerXAnchor constraintEqualToAnchor:root.centerXAnchor],
        [label.centerYAnchor constraintEqualToAnchor:root.centerYAnchor],
        [label.leadingAnchor constraintGreaterThanOrEqualToAnchor:root.leadingAnchor constant:12],
        [label.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-12],
    ]];

    self.titleLabel = label;
    self.view = root;
}

- (void)viewWillAppear {
    [super viewWillAppear];

    [[PlaybackHub sharedHub] addObserver:self];
    [self refreshDisplay];

    if (self.positionTimer == nil) {
        // 用 block 版 NSTimer + __weak self，避免 target:self 版本对 self 的强引用
        // 造成"定时器不失效、控制器就无法 dealloc"的循环引用；即便宿主未来在某条
        // 路径上跳过了 viewWillDisappear，self 依旧能在最后一个强引用释放后正常
        // dealloc，定时器会在下一次 tick 探测到 weakSelf 为 nil 后自行 invalidate。
        __weak __typeof__(self) weakSelf = self;
        self.positionTimer = [NSTimer scheduledTimerWithTimeInterval:0.25
                                                               repeats:YES
                                                                 block:^(NSTimer *timer) {
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) {
                [timer invalidate];
                return;
            }
            [strongSelf refreshDisplay];
        }];
    }
}

- (void)viewWillDisappear {
    [super viewWillDisappear];
    [self.positionTimer invalidate];
    self.positionTimer = nil;
}

- (void)dealloc {
    [self.positionTimer invalidate];
    [[PlaybackHub sharedHub] removeObserver:self];
}

#pragma mark - PlaybackHubObserving

- (void)playbackHubDidChange {
    [self refreshDisplay];
}

#pragma mark - 显示刷新

- (void)refreshDisplay {
    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) {
        self.titleLabel.stringValue = kPlaceholderText;
        return;
    }

    openlyrics::TrackMeta meta = [hub currentTrack];

    // 细粒度位置直接轮询 playback_control，比 hub 里逐秒/seek 更新的快照更平滑；
    // playback_control::get() 理论上应始终可用（core 服务），is_empty() 兜底仅防御性处理。
    auto pc = playback_control::get();
    int64_t posMs = pc.is_empty() ? [hub positionMs]
                                   : static_cast<int64_t>(pc->playback_get_position() * 1000.0 + 0.5);
    if (posMs < 0) posMs = 0;

    NSString *title = meta.title.empty() ? @"(未知曲目)"
                                          : [NSString stringWithUTF8String:meta.title.c_str()];

    long long totalSeconds = posMs / 1000;
    long long minutes = totalSeconds / 60;
    long long seconds = totalSeconds % 60;

    self.titleLabel.stringValue = [NSString stringWithFormat:@"%@ — %02lld:%02lld", title, minutes, seconds];
}

@end
