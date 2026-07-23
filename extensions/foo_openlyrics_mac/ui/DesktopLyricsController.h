// DesktopLyricsController.h
// foo_openlyrics_mac —— Plan 7 Task 2：桌面歌词浮动透明窗口控制器。
#pragma once

#import <Cocoa/Cocoa.h>
#import "foobar2000-mac-helpers.h"
#import "PlaybackBridge.h"

#define DesktopLyricsController FB2K_OBJC_CLASS(DesktopLyricsController)

@interface DesktopLyricsController : NSObject <PlaybackHubObserving>

+ (instancetype)sharedController;

// 加载配置、注册通知/观察者、按当前状态决定是否显示。
- (void)start;

// 隐藏窗口、注销观察者与通知。
- (void)stop;

// 配置变更后由 PreferencesViewController 调用，重建显示配置并重新评估可见性。
- (void)reloadConfig;

@end
