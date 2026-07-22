// LyricView.h
// foo_openlyrics_mac —— Plan 2 Task 5：整行高亮 + 平滑滚动的歌词渲染 NSView。
//
// 消费方（LyricPanelController）在两个时机推数据：
//   - 曲目切换 / 歌词检索完成：调用 -setLyricData: 换整份歌词，内部重建缓存的富文本并把
//     滚动位置立即归零（不对上一首歌的滚动状态做跨曲目插值，那样没有意义）。
//   - 播放位置 tick：调用 -setSyncResult: 更新当前行与目标滚动位置；view 自带一个 ~60ms
//     的 NSTimer，把已渲染的滚动偏移逐帧向目标值缓动，画面因此是连续平滑的，不会随外部
//     tick 的节奏（不论 60ms 还是 250ms）一起跳变。
// 渲染细节（drawRect:）：每行的富文本（普通态/高亮态各一份）只在 -setLyricData: 时构建一次
// 并缓存；每帧 tick 只重新计算滚动偏移与当前高亮行索引，不重建 NSAttributedString，
// 避免 60ms 一次的高频重排版开销。
#pragma once

#import <Cocoa/Cocoa.h>
#import "foobar2000-mac-helpers.h"

#include "model/LyricData.h"
#include "sync/SyncEngine.h"

#define LyricView FB2K_OBJC_CLASS(LyricView)

@interface LyricView : NSView

// 主线程调用。传入新一首曲目的歌词数据；data.lines 为空视为"无歌词"，显示占位文案。
- (void)setLyricData:(const openlyrics::LyricData &)data;

// 主线程调用。每次播放位置 tick 后传入 SyncEngine::locate 的结果，驱动高亮与滚动目标。
- (void)setSyncResult:(const openlyrics::SyncResult &)result;

@end
