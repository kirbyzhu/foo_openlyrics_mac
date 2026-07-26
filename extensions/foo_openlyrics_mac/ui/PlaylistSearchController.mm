#import "PlaylistSearchController.h"
#import "PlaylistSearchBridge.h"

#include <string>
#include "search/PlaylistSearchMatcher.h"

@interface PlaylistSearchController () <NSTextFieldDelegate, NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSPanel *panel;
@property(nonatomic, strong) NSTextField *searchField;
@property(nonatomic, strong) NSTableView *resultTable;
@property(nonatomic, strong) PlaylistSnapshot *snapshot;
@end

@implementation PlaylistSearchController {
    std::vector<openlyrics::MatchHit> _hits;
}

+ (instancetype)shared {
    static PlaylistSearchController *inst = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ inst = [[PlaylistSearchController alloc] init]; });
    return inst;
}

- (void)buildPanel {
    NSRect frame = NSMakeRect(0, 0, 480, 320);
    self.panel = [[NSPanel alloc] initWithContentRect:frame
                                            styleMask:(NSWindowStyleMaskTitled |
                                                       NSWindowStyleMaskClosable |
                                                       NSWindowStyleMaskFullSizeContentView)
                                              backing:NSBackingStoreBuffered
                                                defer:YES];
    self.panel.titleVisibility = NSWindowTitleHidden;
    self.panel.titlebarAppearsTransparent = YES;
    self.panel.movableByWindowBackground = YES;
    self.panel.delegate = self;
    self.panel.hidesOnDeactivate = NO;

    NSView *content = self.panel.contentView;

    self.searchField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    self.searchField.translatesAutoresizingMaskIntoConstraints = NO;
    self.searchField.placeholderString = @"搜索当前播放列表（标题/艺术家/专辑，支持拼音）";
    self.searchField.font = [NSFont systemFontOfSize:15];
    self.searchField.delegate = self;
    self.searchField.bezelStyle = NSTextFieldRoundedBezel;
    [content addSubview:self.searchField];

    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = YES;
    self.resultTable = [[NSTableView alloc] initWithFrame:NSZeroRect];
    self.resultTable.headerView = nil;
    self.resultTable.rowHeight = 22;
    self.resultTable.dataSource = self;
    self.resultTable.delegate = self;
    self.resultTable.doubleAction = @selector(locateSelected);
    self.resultTable.target = self;
    NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:@"disp"];
    col.width = 440;
    [self.resultTable addTableColumn:col];
    scroll.documentView = self.resultTable;
    [content addSubview:scroll];

    [NSLayoutConstraint activateConstraints:@[
        [self.searchField.topAnchor constraintEqualToAnchor:content.topAnchor constant:28],
        [self.searchField.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [self.searchField.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
        [scroll.topAnchor constraintEqualToAnchor:self.searchField.bottomAnchor constant:10],
        [scroll.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
        [scroll.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-16],
    ]];
}

- (void)showOrFocus {
    if (self.panel == nil) [self buildPanel];
    if (!self.panel.isVisible) {
        self.snapshot = [PlaylistSearchBridge snapshotActivePlaylist];
        self.searchField.stringValue = @"";
        [self refilter];
        NSWindow *host = NSApp.keyWindow ?: NSApp.mainWindow;
        if (host) {
            NSRect hf = host.frame;
            NSRect pf = self.panel.frame;
            NSPoint origin = NSMakePoint(NSMidX(hf) - pf.size.width / 2,
                                         NSMidY(hf) - pf.size.height / 2);
            [self.panel setFrameOrigin:origin];
        } else {
            [self.panel center];
        }
        [self.panel makeKeyAndOrderFront:nil];
    }
    [self.panel makeFirstResponder:self.searchField];
}

- (void)refilter {
    std::string q = self.searchField.stringValue.lowercaseString.UTF8String ?: "";
    if (self.snapshot) {
        _hits = openlyrics::matchPlaylist([self.snapshot records], q);
    } else {
        _hits.clear();
    }
    [self.resultTable reloadData];
    if (!_hits.empty()) {
        [self.resultTable selectRowIndexes:[NSIndexSet indexSetWithIndex:0]
                      byExtendingSelection:NO];
    }
}

- (void)controlTextDidChange:(NSNotification *)obj { [self refilter]; }

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    return (NSInteger)_hits.size();
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)col row:(NSInteger)row {
    NSTableCellView *cell = [tableView makeViewWithIdentifier:@"c" owner:self];
    if (cell == nil) {
        cell = [[NSTableCellView alloc] initWithFrame:NSZeroRect];
        cell.identifier = @"c";
        NSTextField *tf = [NSTextField labelWithString:@""];
        tf.translatesAutoresizingMaskIntoConstraints = NO;
        [cell addSubview:tf];
        [NSLayoutConstraint activateConstraints:@[
            [tf.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:4],
            [tf.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        ]];
        cell.textField = tf;
    }
    if (row >= 0 && row < (NSInteger)_hits.size()) {
        cell.textField.stringValue = [self.snapshot displayAt:(NSInteger)_hits[row].index];
    }
    return cell;
}

- (void)locateSelected {
    NSInteger row = self.resultTable.selectedRow;
    if (row < 0 || row >= (NSInteger)_hits.size()) return;
    [self.snapshot locateIndex:(NSInteger)_hits[row].index];
    [self closePanel];
}

- (void)closePanel {
    [self.panel orderOut:nil];
    self.snapshot = nil;
    _hits.clear();
}

// 搜索框内截获方向键/回车/Esc
- (BOOL)control:(NSControl *)control textView:(NSTextView *)textView doCommandBySelector:(SEL)sel {
    if (sel == @selector(moveDown:)) {
        if (_hits.empty()) return YES;
        NSInteger next = MIN(self.resultTable.selectedRow + 1, (NSInteger)_hits.size() - 1);
        if (next >= 0 && next < (NSInteger)_hits.size()) {
            [self.resultTable selectRowIndexes:[NSIndexSet indexSetWithIndex:next] byExtendingSelection:NO];
            [self.resultTable scrollRowToVisible:next];
        }
        return YES;
    }
    if (sel == @selector(moveUp:)) {
        if (_hits.empty()) return YES;
        NSInteger prev = MAX(self.resultTable.selectedRow - 1, 0);
        if (prev >= 0 && prev < (NSInteger)_hits.size()) {
            [self.resultTable selectRowIndexes:[NSIndexSet indexSetWithIndex:prev] byExtendingSelection:NO];
            [self.resultTable scrollRowToVisible:prev];
        }
        return YES;
    }
    if (sel == @selector(insertNewline:)) { [self locateSelected]; return YES; }
    if (sel == @selector(cancelOperation:)) { [self closePanel]; return YES; }
    return NO;
}

- (void)windowDidResignKey:(NSNotification *)notification { [self closePanel]; }

@end
