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

// 停止内部滚动动画定时器。桌面歌词面板隐藏时调用，避免定时器空转。
- (void)stopAnimation;

// 设为 YES 时 drawRect: 不填充背景色，供桌面歌词透明 NSPanel 使用。默认 NO。
@property(nonatomic, assign) BOOL transparentBackground;

// 0 = 显示全部行；>0 时仅渲染当前行附近 maxLines 行（仅 synced 模式生效）。默认 0。
@property(nonatomic, assign) NSInteger maxLines;

// 动态设置无歌词或停止播放时的占位文案（默认 "无歌词"）。
- (void)setPlaceholderText:(NSString *)text;

// 窗口宽度变化后调用，强制重新计算各行文本换行高度。由 controller 在 resize 回调中调用。
- (void)invalidateRowHeights;

// 设置顶部固定标题（如「歌名 — 艺术家」）。传 nil 或空串隐藏标题栏，歌词区占满。
// 标题不随歌词滚动。由 controller 在曲目切换时下发。
- (void)setTitleText:(NSString *)text;

@end
