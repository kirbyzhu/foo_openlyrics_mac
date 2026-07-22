// LyricPanelController.mm
// foo_openlyrics_mac —— Plan 2 Task 2：最小占位歌词面板控制器实现；
// Task 3：向 PlaybackHub 注册/注销，显示 "<title> — <mm:ss>"；
// Task 5：接线 SearchPipeline（Tag→本地）取歌词、LyricView 承载渲染、SyncEngine 驱动
// 高亮与滚动，完成计划二的展示闭环；
// Plan 3 Task 6：本地管线未命中时补一段在线检索（LrcLibProvider）+ 命中自动落盘
// （LyricStore），完成计划三的在线拉取闭环。
//
// 线程切分（对照 task-5-brief.md / task-6-brief.md 的"threading & safety"要求逐条核对过）：
//   - PlaybackHub 的 -playbackHubDidChange 回调固定在主线程触发（PlaybackBridge.h 顶部注释）。
//     本控制器在该回调里，同样在主线程上从 hub 取出当前 TrackMeta（纯值拷贝，无 metadb 访问）。
//   - 真正耗时的检索工作——TagSource 命中与否要经 TagIOAdapter 摸 metadb，LocalFileSource
//     命中与否要摸磁盘，在线段还要发网络请求——整个流程（本地 SearchPipeline::resolve()
//     + 在线 LrcLibProvider::fetch() + 命中后的 LyricStore::save()）被丢到后台并发队列
//     执行，避免阻塞主线程/UI。TagIOAdapter 自己知道要不要把 metadb 访问点切回主线程
//     （见其 .mm 顶部注释），FileSystemAdapter 纯标准库 I/O、HttpAdapter 的 NSURLSession
//     同步阻塞（见其 .mm 顶部注释，明确禁止主线程调用）都天然要求跑在后台线程，两者都
//     不需要 LyricPanelController 关心线程细节，只管把整段检索逻辑扔进后台队列即可。
//   - 检索结果（纯 C++ 值 LyricData）拷贝一份，dispatch 回主线程后才碰 LyricView
//     （AppKit 对象，只能主线程访问）。用自增的 _trackRequestToken 识别"结果算出来时曲目
//     是否已经又切了"，过期结果直接丢弃，不会用旧曲目的歌词错误覆盖新曲目——在线段耗时
//     明显更长（网络往返），这个 token 校验尤其重要：慢的在线结果回来时曲目可能已经
//     再切了几次，token 不匹配就直接丢弃，不会用过期在线结果覆盖新曲目的展示。
//   - 播放位置沿用原有 NSTimer 轮询 playback_control::get()->playback_get_position()
//     （主线程 API），间隔从 0.25s 收紧到 0.06s：SyncEngine::locate 本身是纯内存运算，
//     更高频轮询几乎不增加开销，换来的是 LyricView 高亮切换/滚动目标更新更跟手。
#import "LyricPanelController.h"
#import "stdafx.h"

#import "LyricView.h"
#import "TagIOAdapter.h"
#import "FileSystemAdapter.h"
#import "HttpAdapter.h"

#include "sources/TagSource.h"
#include "sources/LocalFileSource.h"
#include "sources/LrcLibProvider.h"
#include "pipeline/SearchPipeline.h"
#include "store/LyricStore.h"
#include "sync/SyncEngine.h"
#include "model/LyricData.h"

static NSString *const kPlaceholderText = @"未在播放";
// 播放位置轮询间隔：既驱动 SyncEngine::locate 的高亮/滚动目标更新，
// 也顺带刷新顶部状态行的 mm:ss（见 -refreshStatusLine）。
static const NSTimeInterval kSyncTickInterval = 0.06;

@interface LyricPanelController ()
@property(nonatomic, strong) NSTextField *statusLabel;
@property(nonatomic, strong) LyricView *lyricView;
@property(nonatomic, strong) NSTimer *syncTimer;
@property(nonatomic, assign) NSInteger trackRequestToken;
@end

@implementation LyricPanelController {
    // 当前曲目已解析出的歌词；-syncTimer 每 tick 拿它喂 SyncEngine::locate。
    // 只在主线程读写（后台 resolve 完成后 dispatch 回主线程才会写它），无需加锁。
    openlyrics::LyricData _currentLyricData;
}

- (void)loadView {
    // 用 NSBox（NSBoxCustom）取代 CALayer 背景填充，避免额外链接 QuartzCore
    // （沿用 Task 2 的既有取舍，见下方 fillColor 设置）。
    NSBox *root = [[NSBox alloc] initWithFrame:NSMakeRect(0, 0, 320, 200)];
    root.boxType = NSBoxCustom;
    root.borderType = NSNoBorder;
    root.fillColor = [NSColor controlBackgroundColor];

    NSTextField *status = [NSTextField labelWithString:kPlaceholderText];
    status.font = [NSFont systemFontOfSize:11];
    status.textColor = [NSColor secondaryLabelColor];
    status.alignment = NSTextAlignmentCenter;
    status.lineBreakMode = NSLineBreakByTruncatingTail;
    status.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:status];

    LyricView *lyricView = [[LyricView alloc] initWithFrame:NSZeroRect];
    lyricView.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:lyricView];

    [NSLayoutConstraint activateConstraints:@[
        [status.topAnchor constraintEqualToAnchor:root.topAnchor constant:6],
        [status.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:8],
        [status.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-8],

        [lyricView.topAnchor constraintEqualToAnchor:status.bottomAnchor constant:4],
        [lyricView.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [lyricView.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [lyricView.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],
    ]];

    self.statusLabel = status;
    self.lyricView = lyricView;
    self.view = root;
}

- (void)viewWillAppear {
    [super viewWillAppear];

    [[PlaybackHub sharedHub] addObserver:self];
    [self handleTrackChanged];

    if (self.syncTimer == nil) {
        // block 版 NSTimer + __weak self：避免 target:self 版本对 self 的强引用造成循环引用，
        // 与既有 positionTimer 写法（现改名 syncTimer）一致，理由见原注释未变，此处不再重复。
        __weak __typeof__(self) weakSelf = self;
        self.syncTimer = [NSTimer scheduledTimerWithTimeInterval:kSyncTickInterval
                                                           repeats:YES
                                                             block:^(NSTimer *timer) {
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) {
                [timer invalidate];
                return;
            }
            [strongSelf tickSync];
        }];
    }
}

- (void)viewWillDisappear {
    [super viewWillDisappear];
    // 面板不可见时同步停止后台工作：注销 PlaybackHub 观察者（避免曲目切换仍触发
    // handleTrackChanged 派发后台检索）并让 syncTimer 停止轮询播放位置。
    // PlaybackHub 用 NSHashTable weakObjectsHashTable 存观察者，addObject:/removeObject:
    // 天然去重，viewWillAppear 里再次 addObserver:self 不会产生重复回调；
    // hub 对观察者只持弱引用，这里不构成 self 与 hub 之间的循环引用。
    [[PlaybackHub sharedHub] removeObserver:self];
    [self.syncTimer invalidate];
    self.syncTimer = nil;
}

- (void)dealloc {
    // ARC 下 -dealloc 在释放最后一个强引用的线程上执行，不保证是主线程（宿主可能
    // 在非主线程释放被包装的 NSViewController），两处调用因此都设计为可在任意线程安全调用：
    //   1. removeObserver: 内部用 NSLock 保护 _observers 表，可放心跨线程调用。
    //   2. NSTimer -invalidate 本身线程安全；正常路径下 viewWillDisappear 已经在主线程
    //      invalidate 并置 nil，走到这里多半是 self.syncTimer 已为 nil（no-op）。
    [self.syncTimer invalidate];
    [[PlaybackHub sharedHub] removeObserver:self];
}

#pragma mark - PlaybackHubObserving

- (void)playbackHubDidChange {
    [self handleTrackChanged];
}

#pragma mark - 曲目切换：主线程取 TrackMeta，后台跑检索管线

- (void)handleTrackChanged {
    // 每次曲目切换都作废之前还在后台跑的检索请求：token 递增后，旧请求即便算完，
    // completion 里的 token 比对也会发现自己过期，直接丢弃结果，不会用旧曲目歌词
    // 覆盖新曲目（含"没有下一首、纯粹停止播放"的情况——此时 hub hasTrack 为 NO）。
    self.trackRequestToken += 1;
    const NSInteger requestToken = self.trackRequestToken;

    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) {
        _currentLyricData = openlyrics::LyricData{};
        [self.lyricView setLyricData:_currentLyricData];
        self.statusLabel.stringValue = kPlaceholderText;
        return;
    }

    openlyrics::TrackMeta meta = [hub currentTrack];  // 值拷贝，主线程读，安全传给后台闭包

    NSString *title = meta.title.empty() ? @"(未知曲目)" : [NSString stringWithUTF8String:meta.title.c_str()];
    self.statusLabel.stringValue = [NSString stringWithFormat:@"%@ · 检索歌词中…", title];

    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        // 每次检索用局部的适配器/源/管线实例，互不共享可变状态，天然线程安全，
        // 不需要为并发的多次曲目切换请求加锁。
        openlyrics::TagIOAdapter tagAdapter;
        openlyrics::FileSystemAdapter fsAdapter;
        openlyrics::TagSource tagSource(tagAdapter);
        openlyrics::LocalFileSource localSource(fsAdapter);
        openlyrics::SearchPipeline pipeline({&tagSource, &localSource});

        openlyrics::LyricData resolved;
        bool found = pipeline.resolve(meta, resolved);

        // 诊断用：SearchPipeline::resolve 本身按 bool 短路，不暴露具体是哪个子 source
        // 命中；这里额外做一次 tagSource.fetch 探测（TagIOAdapter 读的是已缓存的
        // file_info，无磁盘 I/O，开销可忽略）区分"内嵌标签"与"同目录 .lrc 文件"，
        // 方便用户在控制台核对歌词到底从哪来。不影响真正走 SearchPipeline 的解析结果。
        std::string sourceLabel = "none";
        if (found) {
            openlyrics::LyricData tagProbe;
            sourceLabel = tagSource.fetch(meta, tagProbe) ? "tag" : "local";
        }

        bool onlineSaved = false;
        if (!found) {
            // 本地（内嵌标签 + 同目录 .lrc）都未命中：切状态行到"在线获取中…"，
            // 让用户知道面板仍在工作而非卡死，再尝试 LrcLib 在线检索。
            dispatch_async(dispatch_get_main_queue(), ^{
                __typeof__(self) strongSelf = weakSelf;
                if (strongSelf == nil) return;
                if (strongSelf.trackRequestToken != requestToken) return;  // 曲目已再次切换
                strongSelf.statusLabel.stringValue = [NSString stringWithFormat:@"%@ · 在线获取中…", title];
            });

            openlyrics::HttpAdapter httpAdapter;
            openlyrics::LrcLibProvider onlineProvider(httpAdapter);
            openlyrics::LyricData onlineData;
            if (onlineProvider.fetch(meta, onlineData)) {
                found = true;
                resolved = onlineData;
                sourceLabel = "online";

                // 命中后自动落盘：写到 <音频名>.lrc，下次同一曲目本地精确步就能直接
                // 命中，不必再打网络请求。best-effort——写盘失败（如目录不可写）不影响
                // 本次已经取到的在线歌词照常展示，只在控制台日志里体现保存与否。
                openlyrics::LyricStore store(fsAdapter);
                onlineSaved = store.save(meta, onlineData);
            }
        }

        if (!found) resolved = openlyrics::LyricData{};

        // Task 5 补丁：Bug #1（get_path() 未转原生路径）修好后，留一行诊断日志方便用户在
        // 控制台面板核对——曲目切换时到底解析出了哪条原生路径、有没有摸到歌词文件，以及
        // 是本地（tag/local）还是在线（online）命中、在线命中时是否已落盘。
        // console:: 命名空间函数全线程安全（console.h:4），可以直接在后台队列调用。
        FB2K_console_print("foo_openlyrics: native path=", meta.path.c_str(),
                            found ? "  lyric=matched source=" : "  lyric=not-found source=",
                            sourceLabel.c_str(),
                            sourceLabel == "online" ? (onlineSaved ? "  saved=yes" : "  saved=no") : "");

        dispatch_async(dispatch_get_main_queue(), ^{
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) return;
            if (strongSelf.trackRequestToken != requestToken) return;  // 曲目已再次切换，丢弃过期结果

            strongSelf->_currentLyricData = resolved;
            [strongSelf.lyricView setLyricData:resolved];
            strongSelf.statusLabel.stringValue = found ? title : [NSString stringWithFormat:@"%@ · 未找到歌词", title];
        });
    });
}

#pragma mark - 播放位置 tick：喂 SyncEngine，驱动 LyricView 高亮/滚动

- (void)tickSync {
    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) return;

    // 细粒度位置直接轮询 playback_control，比 hub 里逐秒/seek 更新的快照更平滑；
    // playback_control::get() 理论上应始终可用（core 服务），is_empty() 兜底仅防御性处理。
    auto pc = playback_control::get();
    int64_t posMs = pc.is_empty() ? [hub positionMs]
                                   : static_cast<int64_t>(pc->playback_get_position() * 1000.0 + 0.5);
    if (posMs < 0) posMs = 0;

    openlyrics::SyncResult result = openlyrics::SyncEngine::locate(_currentLyricData, posMs);
    [self.lyricView setSyncResult:result];
}

@end
