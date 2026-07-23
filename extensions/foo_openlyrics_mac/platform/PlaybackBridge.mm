// PlaybackBridge.mm
// foo_openlyrics_mac —— Plan 2 Task 3：PlaybackHub 单例实现 + play_callback_static 桥接。
#import "PlaybackBridge.h"
#import "stdafx.h"
#import "DesktopLyricsController.h"

#include <string>

using openlyrics::TrackMeta;

namespace {

// 兜底：当 metadb_handle::get_info() 尚未返回可用 tag（刚切歌、tag 还没读出来）时，
// 用文件名（去掉目录与扩展名）充当标题占位，保证面板不会显示空字符串。
std::string TitleFromPath(const std::string &path) {
    if (path.empty()) return path;
    size_t slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return name;
}

// get_path() 返回的是 fb2k 带 scheme 的规范路径（如 "file:///Users/..."），不是
// std::ifstream 能直接吃的原生 POSIX 路径——这是 Task 5 本地 .lrc 歌词读不到的根因之一
// （FileSystemAdapter.mm 内部用 std::ifstream 打开这个字符串会直接失败）。用 SDK 提供的
// filesystem::g_get_native_path 转换（SDK/filesystem.h:97-98）；转换失败（远程/非文件源）
// 时退回原始路径，保证 meta.path 始终非空。转换后的原生路径同时供标题兜底
// （TitleFromPath）与下游 LocalFileSource 使用。
std::string ToNativePath(const std::string &rawPath) {
    if (rawPath.empty()) return rawPath;
    pfc::string8 native;
    if (filesystem::g_get_native_path(rawPath.c_str(), native)) {
        return std::string(native.c_str());
    }
    return rawPath;
}

TrackMeta MakeTrackMeta(const metadb_handle_ptr &track) {
    TrackMeta meta;
    if (track.is_empty()) return meta;

    meta.path = ToNativePath(track->get_path());

    // metadb_handle::get_info(file_info&) 已标注 Obsolete（SDK/metadb_handle.h:66-69），
    // 改用 get_info_ref() 家族：SDK/metadb_handle.h:111 声明的
    // bool get_info_ref(metadb_info_container::ptr&) 保留了与旧接口一致的
    // "有缓存信息才返回 true" 语义，容器的 info()（SDK/metadb_handle.h:14）
    // 返回 const file_info&，供下面按原字段原样读取。
    metadb_info_container::ptr infoRef;
    if (track->get_info_ref(infoRef)) {
        const file_info &info = infoRef->info();
        const char *title = info.meta_get_title(nullptr);
        if (title != nullptr) meta.title = title;

        if (info.meta_get_count_by_name("artist") > 0) {
            const char *artist = info.meta_get("artist", 0);
            if (artist != nullptr) meta.artist = artist;
        }
        if (info.meta_get_count_by_name("album") > 0) {
            const char *album = info.meta_get("album", 0);
            if (album != nullptr) meta.album = album;
        }
    }

    if (meta.title.empty()) meta.title = TitleFromPath(meta.path);

    // metadb_handle::get_length() 为 SDK/metadb_handle.h:163 声明的 helper，返回秒数。
    meta.lengthMs = static_cast<int64_t>(track->get_length() * 1000.0 + 0.5);

    return meta;
}

int64_t CurrentPositionMs() {
    auto pc = playback_control::get();
    if (pc.is_empty()) return 0;
    // playback_control::playback_get_position()，SDK/playback_control.h:76，返回秒数（double）。
    return static_cast<int64_t>(pc->playback_get_position() * 1000.0 + 0.5);
}

int64_t SecondsToMs(double seconds) {
    return static_cast<int64_t>(seconds * 1000.0 + 0.5);
}

}  // namespace

// --- PlaybackHub 内部写入接口，仅供本文件内的 PlaybackCallback 使用，不对外暴露。 ---
@interface PlaybackHub ()
- (void)handleNewTrack:(const openlyrics::TrackMeta &)meta positionMs:(int64_t)posMs;
- (void)handlePositionMs:(int64_t)posMs;
- (void)handleStop;
@end

@implementation PlaybackHub {
    openlyrics::TrackMeta _track;
    int64_t _positionMs;
    BOOL _hasTrack;
    NSHashTable<id<PlaybackHubObserving>> *_observers;
    // _observersLock：保护 _observers 表的增删/遍历。play_callback 系列回调始终在主线程
    // 触发 addObserver:/removeObserver:/notifyObservers，但 ARC 下面板的 -dealloc 可能
    // 在宿主释放最后一个强引用的任意线程上执行（不保证是主线程），届时也会调用
    // removeObserver:，因此这里不能再假定"全程主线程"，需要显式加锁。
    NSLock *_observersLock;
}

+ (instancetype)sharedHub {
    static PlaybackHub *instance = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        instance = [[PlaybackHub alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self != nil) {
        _positionMs = 0;
        _hasTrack = NO;
        _observers = [NSHashTable weakObjectsHashTable];
        _observersLock = [[NSLock alloc] init];
    }
    return self;
}

// currentTrack/positionMs/hasTrack 及其写入方（handleNewTrack:/handlePositionMs:/handleStop）
// 未加锁：写入方只在 play_callback 的主线程回调里触发，读取方（refreshDisplay 由
// viewWillAppear/playbackHubDidChange/主线程 NSTimer 触发）同样全程在主线程，
// 二者不跨线程交叉，不属于本次要修的观察者表竞态，故不额外加锁。

- (openlyrics::TrackMeta)currentTrack {
    return _track;
}

- (int64_t)positionMs {
    return _positionMs;
}

- (BOOL)hasTrack {
    return _hasTrack;
}

- (void)addObserver:(id<PlaybackHubObserving>)observer {
    [_observersLock lock];
    [_observers addObject:observer];
    [_observersLock unlock];
}

- (void)removeObserver:(id<PlaybackHubObserving>)observer {
    // 面板 -dealloc 可能在非主线程调用到这里（见 _observersLock 的注释），
    // 加锁后即可安全地与 notifyObservers/addObserver 并发执行。
    [_observersLock lock];
    [_observers removeObject:observer];
    [_observersLock unlock];
}

- (void)notifyObservers {
    // play_callback 系列回调本身全程在主线程触发（SDK/play_callback.h 顶部注释），
    // 但 removeObserver: 可能来自任意线程的 -dealloc，所以这里仍需加锁保护 _observers。
    // 在锁内取 allObjects 快照，随后立即解锁，再在锁外逐个调用观察者回调——
    // 避免持锁期间调用外部代码：如果某个观察者的回调又同步调用回 addObserver:/
    // removeObserver:（重入），持锁调用就会自死锁；锁外调用则完全规避这个问题。
    [_observersLock lock];
    NSArray<id<PlaybackHubObserving>> *snapshot = [_observers allObjects];
    [_observersLock unlock];

    for (id<PlaybackHubObserving> observer in snapshot) {
        [observer playbackHubDidChange];
    }
}

- (void)handleNewTrack:(const openlyrics::TrackMeta &)meta positionMs:(int64_t)posMs {
    _track = meta;
    _positionMs = posMs;
    _hasTrack = YES;
    [self notifyObservers];
}

- (void)handlePositionMs:(int64_t)posMs {
    _positionMs = posMs;
    // 逐秒/seek 位置刷新不通知观察者：面板自行用 NSTimer 轮询 playback_control 取更平滑的
    // 数值，这里没必要为每次 tick 都触发一轮全量 UI 刷新。
}

- (void)handleStop {
    _track = openlyrics::TrackMeta{};
    _positionMs = 0;
    _hasTrack = NO;
    [self notifyObservers];
}

- (void)notifyLyricChanged {
    [_observersLock lock];
    NSArray<id<PlaybackHubObserving>> *snapshot = [_observers allObjects];
    [_observersLock unlock];

    for (id<PlaybackHubObserving> observer in snapshot) {
        if ([observer respondsToSelector:@selector(playbackHubLyricDidChange)]) {
            [observer playbackHubLyricDidChange];
        }
    }
}

@end

namespace {

// play_callback_static：随组件加载自动注册（见 SDK/play_callback.h:107-113），
// 不依赖任何面板实例存在，从组件加载起即持续把播放状态写入 PlaybackHub —— 对应
// "host 可能创建 0 个或多个面板" 的要求：即使当下没有面板打开，hub 状态也保持最新。
// 对照 foo_sample/playback_state.cpp 里 play_callback_impl_base 的用法，本类改用
// play_callback_static + FB2K_SERVICE_FACTORY，注册时机交给 core（在服务可用后才
// 实例化并调用 get_flags()），避免像该 impl_base 版本那样在 C++ 静态对象构造期
// 直接调用 play_callback_manager::get() 可能引发的初始化顺序问题。
class PlaybackCallback : public play_callback_static {
public:
    unsigned get_flags() override {
        return flag_on_playback_new_track | flag_on_playback_stop |
               flag_on_playback_seek | flag_on_playback_pause | flag_on_playback_time;
    }

    void on_playback_starting(play_control::t_track_command, bool) override {}

    void on_playback_new_track(metadb_handle_ptr p_track) override {
        static bool s_deskLyricsInited = false;
        if (!s_deskLyricsInited) {
            s_deskLyricsInited = true;
            dispatch_async(dispatch_get_main_queue(), ^{
                [[DesktopLyricsController sharedController] start];
            });
        }
        TrackMeta meta = MakeTrackMeta(p_track);
        [[PlaybackHub sharedHub] handleNewTrack:meta positionMs:CurrentPositionMs()];
    }

    void on_playback_stop(play_control::t_stop_reason) override {
        [[PlaybackHub sharedHub] handleStop];
    }

    void on_playback_seek(double p_time) override {
        [[PlaybackHub sharedHub] handlePositionMs:SecondsToMs(p_time)];
    }

    void on_playback_pause(bool) override {}

    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info &) override {}
    void on_playback_dynamic_info_track(const file_info &) override {}

    void on_playback_time(double p_time) override {
        [[PlaybackHub sharedHub] handlePositionMs:SecondsToMs(p_time)];
    }

    void on_volume_change(float) override {}
};

FB2K_SERVICE_FACTORY(PlaybackCallback);

}  // namespace
