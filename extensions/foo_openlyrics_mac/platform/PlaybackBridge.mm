// PlaybackBridge.mm
// foo_openlyrics_mac —— Plan 2 Task 3：PlaybackHub 单例实现 + play_callback_static 桥接。
#import "PlaybackBridge.h"
#import "stdafx.h"

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

TrackMeta MakeTrackMeta(const metadb_handle_ptr &track) {
    TrackMeta meta;
    if (track.is_empty()) return meta;

    meta.path = track->get_path();

    file_info_impl info;
    if (track->get_info(info)) {
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
    }
    return self;
}

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
    [_observers addObject:observer];
}

- (void)removeObserver:(id<PlaybackHubObserving>)observer {
    [_observers removeObject:observer];
}

- (void)notifyObservers {
    // play_callback 全程在主线程触发（SDK/play_callback.h 顶部注释），这里无需额外派发。
    // allObjects 先取快照，防止观察者在回调里同步增删注册表导致遍历期间被修改。
    for (id<PlaybackHubObserving> observer in [_observers allObjects]) {
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
