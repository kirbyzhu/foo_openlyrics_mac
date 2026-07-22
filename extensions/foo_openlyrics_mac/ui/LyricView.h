// LyricView.h
// foo_openlyrics_mac —— Plan 2 Task 5：整行高亮 + 平滑滚动的歌词渲染 NSView；
// Plan 6 Task 4：新增 applyDisplayConfig: 支持偏好设置中的字体/颜色/对齐/行距配置。
#pragma once

#import <Cocoa/Cocoa.h>
#import "foobar2000-mac-helpers.h"

#include "model/LyricData.h"
#include "sync/SyncEngine.h"
#include "config/AppConfig.h"

#define LyricView FB2K_OBJC_CLASS(LyricView)

@interface LyricView : NSView

// 主线程调用。传入新一首曲目的歌词数据；data.lines 为空视为"无歌词"，显示占位文案。
- (void)setLyricData:(const openlyrics::LyricData &)data;

// 主线程调用。每次播放位置 tick 后传入 SyncEngine::locate 的结果，驱动高亮与滚动目标。
- (void)setSyncResult:(const openlyrics::SyncResult &)result;

// 应用显示配置，重建已缓存的富文本行。
- (void)applyDisplayConfig:(const openlyrics::DisplayConfig &)config;

@end
