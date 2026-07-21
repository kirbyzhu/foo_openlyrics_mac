// LyricPanelController.mm
// foo_openlyrics_mac —— Plan 2 Task 2：最小占位歌词面板控制器实现。
#import "LyricPanelController.h"

@implementation LyricPanelController

- (void)loadView {
    // 用 NSBox（NSBoxCustom）取代 CALayer 背景填充，避免额外链接 QuartzCore。
    NSBox *root = [[NSBox alloc] initWithFrame:NSMakeRect(0, 0, 320, 200)];
    root.boxType = NSBoxCustom;
    root.borderType = NSNoBorder;
    root.fillColor = [NSColor controlBackgroundColor];

    NSTextField *label = [NSTextField labelWithString:@"OpenLyrics"];
    label.font = [NSFont systemFontOfSize:18];
    label.alignment = NSTextAlignmentCenter;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:label];

    [NSLayoutConstraint activateConstraints:@[
        [label.centerXAnchor constraintEqualToAnchor:root.centerXAnchor],
        [label.centerYAnchor constraintEqualToAnchor:root.centerYAnchor],
    ]];

    self.view = root;
}

@end
