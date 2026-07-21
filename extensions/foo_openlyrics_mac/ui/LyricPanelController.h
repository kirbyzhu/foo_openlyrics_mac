// LyricPanelController.h
// foo_openlyrics_mac —— Plan 2 Task 2：最小占位歌词面板控制器。
// 纯程序化 NSViewController，不用 xib；loadView 里放一个带背景色的 NSBox 与居中 NSTextField。
#pragma once

#import <Cocoa/Cocoa.h>
#import "foobar2000-mac-helpers.h"

// 经 FB2K_OBJC_CLASS 按组件自有后缀（见 foobar2000-mac-class-suffix.h）重命名，
// 避免与其他已装组件里的同名 Obj-C 类冲突。
#define LyricPanelController FB2K_OBJC_CLASS(LyricPanelController)

@interface LyricPanelController : NSViewController
@end
