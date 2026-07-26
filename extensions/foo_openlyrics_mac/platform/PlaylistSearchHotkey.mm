#import "stdafx.h"
#import <Cocoa/Cocoa.h>
#import "PlaylistSearchController.h"

namespace {

static id g_monitor = nil;

class PlaylistSearchHotkey : public initquit {
public:
    void on_init() override {
        if (g_monitor != nil) return;
        g_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                          handler:^NSEvent *(NSEvent *event) {
            BOOL isF3 = (event.keyCode == 99);
            BOOL isCmdF = (event.modifierFlags & NSEventModifierFlagCommand) &&
                          (event.modifierFlags & (NSEventModifierFlagShift | NSEventModifierFlagOption | NSEventModifierFlagControl)) == 0 &&
                          [event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"f"];
            if (isF3 || isCmdF) {
                [[PlaylistSearchController shared] showOrFocus];
                return nil;  // 吞掉事件，覆盖宿主处理
            }
            return event;
        }];
    }

    void on_quit() override {
        // on_quit 在主线程；直接移除
        if (g_monitor != nil) {
            [NSEvent removeMonitor:g_monitor];
            g_monitor = nil;
        }
    }
};

initquit_factory_t<PlaylistSearchHotkey> g_playlist_search_hotkey_factory;

}  // namespace
