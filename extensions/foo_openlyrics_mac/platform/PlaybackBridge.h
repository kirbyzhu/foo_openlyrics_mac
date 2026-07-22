// PlaybackBridge.h
// foo_openlyrics_mac —— Plan 2 Task 3：play_callback 实现 + PlaybackHub 单例 broker。
//
// PlaybackHub 是全组件唯一的播放状态广播中心：play_callback（实现见 PlaybackBridge.mm，
// 全程在 SDK 主线程回调中运行，见 SDK/play_callback.h 顶部注释）把当前曲目/播放位置写入
// hub，各面板控制器作为观察者订阅变化。host 可能创建 0 个或多个面板实例，hub 与面板生命
// 周期解耦：hub 从组件加载起即通过 play_callback_static 持续追踪播放状态，与是否存在面板
// 无关；面板只是随时可插拔的观察者。
//
// hub 与观察者之间用 NSHashTable 弱引用表存储，面板正常应在 dealloc 时调用 removeObserver:
// 尽早解除关注，但即便遗漏也不会造成悬挂指针。
#pragma once

#import <Cocoa/Cocoa.h>
#import "foobar2000-mac-helpers.h"

#include "model/TrackMeta.h"

// 类名/协议名按项目既有规范加组件专属后缀，避免与宿主进程中其他组件的同名 Obj-C 符号冲突
// （见 foobar2000-mac-class-suffix.h、component_entry.mm 对 LyricPanelController 的处理）。
#define PlaybackHub FB2K_OBJC_CLASS(PlaybackHub)
#define PlaybackHubObserving FB2K_OBJC_CLASS(PlaybackHubObserving)

// 面板等 UI 组件实现该协议以接收播放状态变化通知（新曲目、停止）；回调固定在主线程触发。
@protocol PlaybackHubObserving <NSObject>
- (void)playbackHubDidChange;
@end

@interface PlaybackHub : NSObject

+ (instancetype)sharedHub;

// 当前曲目元数据；无曲目播放（未开始播放或已停止）时返回默认构造的空 TrackMeta。
- (openlyrics::TrackMeta)currentTrack;
// hub 最近一次从 play_callback（on_playback_new_track/on_playback_time/on_playback_seek）
// 收到的播放位置快照，单位毫秒。面板若需要更平滑的刷新，可自行更高频轮询
// playback_control::get()->playback_get_position()（见 SDK/playback_control.h），
// 此字段仅作粗粒度兜底/供不跑定时器的观察者使用。
- (int64_t)positionMs;
// 当前是否有曲目在播放/暂停中；on_playback_stop 后为 NO。
- (BOOL)hasTrack;

- (void)addObserver:(id<PlaybackHubObserving>)observer;
- (void)removeObserver:(id<PlaybackHubObserving>)observer;

@end
