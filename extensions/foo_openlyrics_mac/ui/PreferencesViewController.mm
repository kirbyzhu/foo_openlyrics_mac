#import "PreferencesViewController.h"
#import "stdafx.h"

#include "config/AppConfig.h"
#include "ConfigAdapter.h"
#import "DesktopLyricsController.h"

static NSString *const kDragDropType = @"foo_openlyrics.source_row";

static const CGFloat kMinPanelWidth = 200.0;
static const CGFloat kMinPanelHeight = 60.0;

@interface PreferencesViewController () <NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic, assign) openlyrics::AppConfig config;
@property(nonatomic, strong) NSTableView *sourceTable;

@property(nonatomic, strong) NSTextField *fontLabel;
@property(nonatomic, strong) NSColorWell *normalColorWell;
@property(nonatomic, strong) NSColorWell *highlightColorWell;
@property(nonatomic, strong) NSPopUpButton *alignmentPopup;
@property(nonatomic, strong) NSSlider *lineSpacingSlider;
@property(nonatomic, strong) NSTextField *lineSpacingLabel;
@property(nonatomic, strong) NSTextField *previewLabel;

@property(nonatomic, strong) NSTextField *defaultOffsetField;
@property(nonatomic, strong) NSTextField *httpTimeoutField;
@property(nonatomic, strong) NSTextField *maxFailuresField;
@property(nonatomic, strong) NSPopUpButton *logLevelPopup;

// 桌面歌词
@property(nonatomic, strong) NSButton *deskEnabledCheck;
@property(nonatomic, strong) NSButton *deskBackgroundCheck;
@property(nonatomic, strong) NSButton *deskShowTitleCheck;
@property(nonatomic, strong) NSTextField *deskFontSizeField;
@property(nonatomic, strong) NSColorWell *deskNormalColorWell;
@property(nonatomic, strong) NSColorWell *deskHighlightColorWell;
@property(nonatomic, strong) NSPopUpButton *deskAlignmentPopup;
@property(nonatomic, strong) NSSlider *deskLineSpacingSlider;
@property(nonatomic, strong) NSTextField *deskLineSpacingLabel;
@property(nonatomic, strong) NSTextField *deskMaxLinesField;
@property(nonatomic, strong) NSTextField *deskWindowWidthField;
@property(nonatomic, strong) NSTextField *deskWindowHeightField;
@end

@implementation PreferencesViewController

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 360)];
    self.config = openlyrics::ConfigAdapter().load();

    NSTabView *tabs = [[NSTabView alloc] initWithFrame:NSZeroRect];
    tabs.translatesAutoresizingMaskIntoConstraints = NO;

    [tabs addTabViewItem:[self sourcesTab]];
    [tabs addTabViewItem:[self displayTab]];
    [tabs addTabViewItem:[self advancedTab]];
    [tabs addTabViewItem:[self deskLyricsTab]];

    [self.view addSubview:tabs];
    [NSLayoutConstraint activateConstraints:@[
        [tabs.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:8],
        [tabs.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:8],
        [tabs.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-8],
        [tabs.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-8],
    ]];
}

#pragma mark - Sources Tab

- (NSTabViewItem *)sourcesTab {
    NSTabViewItem *item = [[NSTabViewItem alloc] initWithIdentifier:@"sources"];
    item.label = @"数据源";

    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.hasVerticalScroller = YES;
    scroll.translatesAutoresizingMaskIntoConstraints = NO;

    NSTableView *tv = [[NSTableView alloc] initWithFrame:NSZeroRect];
    tv.headerView = nil;
    tv.rowHeight = 24;
    tv.dataSource = self;
    tv.delegate = self;
    [tv registerForDraggedTypes:@[kDragDropType]];
    tv.usesAlternatingRowBackgroundColors = YES;

    NSTableColumn *nameCol = [[NSTableColumn alloc] initWithIdentifier:@"name"];
    nameCol.title = @"";
    nameCol.width = 300;
    [tv addTableColumn:nameCol];

    NSTableColumn *enabledCol = [[NSTableColumn alloc] initWithIdentifier:@"enabled"];
    enabledCol.title = @"";
    enabledCol.width = 60;
    [tv addTableColumn:enabledCol];

    scroll.documentView = tv;
    self.sourceTable = tv;

    NSView *container = [[NSView alloc] initWithFrame:NSZeroRect];
    [container addSubview:scroll];
    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:container.topAnchor],
        [scroll.leadingAnchor constraintEqualToAnchor:container.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:container.trailingAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:container.bottomAnchor],
    ]];

    item.view = container;
    return item;
}

- (NSString *)displayNameForSource:(const std::string&)key {
    if (key == "tag") return @"内嵌标签";
    if (key == "local") return @"本地文件 (.lrc)";
    if (key == "lrclib") return @"LrcLib (在线)";
    if (key == "netease") return @"网易云音乐";
    if (key == "qqmusic") return @"QQ 音乐";
    return [NSString stringWithUTF8String:key.c_str()];
}

#pragma mark - Display Tab

- (NSTabViewItem *)displayTab {
    NSTabViewItem *item = [[NSTabViewItem alloc] initWithIdentifier:@"display"];
    item.label = @"显示";

    NSView *v = [[NSView alloc] initWithFrame:NSZeroRect];

    // 字体
    NSTextField *fontCap = [NSTextField labelWithString:@"字体："];
    fontCap.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:fontCap];

    NSButton *fontBtn = [NSButton buttonWithTitle:@"选择…" target:self action:@selector(chooseFont:)];
    fontBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:fontBtn];

    NSTextField *fontLbl = [NSTextField labelWithString:@""];
    fontLbl.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:fontLbl];
    self.fontLabel = fontLbl;

    // 颜色
    NSTextField *normalCap = [NSTextField labelWithString:@"常规色："];
    normalCap.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:normalCap];

    NSColorWell *normalWell = [NSColorWell new];
    normalWell.translatesAutoresizingMaskIntoConstraints = NO;
    normalWell.target = self;
    normalWell.action = @selector(colorChanged:);
    [v addSubview:normalWell];
    self.normalColorWell = normalWell;

    NSTextField *hlCap = [NSTextField labelWithString:@"高亮色："];
    hlCap.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:hlCap];

    NSColorWell *hlWell = [NSColorWell new];
    hlWell.translatesAutoresizingMaskIntoConstraints = NO;
    hlWell.target = self;
    hlWell.action = @selector(colorChanged:);
    [v addSubview:hlWell];
    self.highlightColorWell = hlWell;

    // 对齐
    NSTextField *alignCap = [NSTextField labelWithString:@"对齐："];
    alignCap.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:alignCap];

    NSPopUpButton *alignPop = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [alignPop addItemsWithTitles:@[@"居中", @"左对齐", @"右对齐"]];
    alignPop.translatesAutoresizingMaskIntoConstraints = NO;
    alignPop.target = self;
    alignPop.action = @selector(alignmentChanged:);
    [v addSubview:alignPop];
    self.alignmentPopup = alignPop;

    // 行距
    NSTextField *spCap = [NSTextField labelWithString:@"行距："];
    spCap.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:spCap];

    NSSlider *spSlider = [[NSSlider alloc] initWithFrame:NSZeroRect];
    spSlider.minValue = 0;
    spSlider.maxValue = 20;
    spSlider.translatesAutoresizingMaskIntoConstraints = NO;
    spSlider.target = self;
    spSlider.action = @selector(lineSpacingChanged:);
    [v addSubview:spSlider];
    self.lineSpacingSlider = spSlider;

    NSTextField *spLbl = [NSTextField labelWithString:@"0"];
    spLbl.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:spLbl];
    self.lineSpacingLabel = spLbl;

    // 预览
    NSTextField *preCap = [NSTextField labelWithString:@"预览："];
    preCap.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:preCap];

    NSTextField *preview = [NSTextField labelWithString:@"[00:12.34] 歌词行预览"];
    preview.alignment = NSTextAlignmentCenter;
    preview.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:preview];
    self.previewLabel = preview;

    NSDictionary *views = NSDictionaryOfVariableBindings(fontCap, fontBtn, fontLbl,
        normalCap, normalWell, hlCap, hlWell, alignCap, alignPop,
        spCap, spSlider, spLbl, preCap, preview);
    for (NSView *sv in views.allValues) {
        [sv setContentHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationHorizontal];
    }

    [v addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
        @"H:|-[fontCap]-[fontBtn]-[fontLbl(<=200)]" options:0 metrics:nil views:views]];
    [v addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
        @"H:|-[normalCap]-[normalWell(44)]-40-[hlCap]-[hlWell(44)]" options:0 metrics:nil views:views]];
    [v addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
        @"H:|-[alignCap]-[alignPop(120)]" options:0 metrics:nil views:views]];
    [v addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
        @"H:|-[spCap]-[spSlider]-[spLbl(36)]-|" options:0 metrics:nil views:views]];
    [v addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
        @"H:|-[preCap]-[preview]-|" options:0 metrics:nil views:views]];

    [v addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
        @"V:|-[fontCap]-[normalCap]-[alignCap]-[spCap]-[preCap]" options:NSLayoutFormatAlignAllLeading metrics:nil views:views]];
    [v addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
        @"V:[fontBtn]-[normalWell]-[alignPop]-[spSlider]-[preview]" options:NSLayoutFormatAlignAllLeft metrics:nil views:views]];
    [v addConstraint:[fontLbl.centerYAnchor constraintEqualToAnchor:fontBtn.centerYAnchor]];
    [v addConstraint:[hlWell.centerYAnchor constraintEqualToAnchor:normalWell.centerYAnchor]];
    [v addConstraint:[fontBtn.centerYAnchor constraintEqualToAnchor:fontCap.centerYAnchor]];
    [v addConstraint:[normalWell.centerYAnchor constraintEqualToAnchor:normalCap.centerYAnchor]];
    [v addConstraint:[hlCap.centerYAnchor constraintEqualToAnchor:normalCap.centerYAnchor]];
    [v addConstraint:[alignPop.centerYAnchor constraintEqualToAnchor:alignCap.centerYAnchor]];
    [v addConstraint:[spSlider.centerYAnchor constraintEqualToAnchor:spCap.centerYAnchor]];
    [v addConstraint:[spLbl.centerYAnchor constraintEqualToAnchor:spCap.centerYAnchor]];
    [v addConstraint:[preview.centerYAnchor constraintEqualToAnchor:preCap.centerYAnchor]];

    item.view = v;
    return item;
}

#pragma mark - Advanced Tab

- (NSTabViewItem *)advancedTab {
    NSTabViewItem *item = [[NSTabViewItem alloc] initWithIdentifier:@"advanced"];
    item.label = @"高级";

    NSView *v = [[NSView alloc] initWithFrame:NSZeroRect];
    NSGridView *grid = [NSGridView gridViewWithViews:@[
        @[[NSTextField labelWithString:@"默认 offset (ms)："],
           ({
               NSTextField *tf = [[NSTextField alloc] initWithFrame:NSZeroRect];
               tf.controlSize = NSControlSizeSmall;
               self.defaultOffsetField = tf;
               tf;
           })],
        @[[NSTextField labelWithString:@"HTTP 超时 (秒)："],
           ({
               NSTextField *tf = [[NSTextField alloc] initWithFrame:NSZeroRect];
               tf.controlSize = NSControlSizeSmall;
               self.httpTimeoutField = tf;
               tf;
           })],
        @[[NSTextField labelWithString:@"最大连续失败次数："],
           ({
               NSTextField *tf = [[NSTextField alloc] initWithFrame:NSZeroRect];
               tf.controlSize = NSControlSizeSmall;
               self.maxFailuresField = tf;
               tf;
           })],
        @[[NSTextField labelWithString:@"日志级别："],
           ({
               NSPopUpButton *pb = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
               [pb addItemsWithTitles:@[@"debug", @"info", @"warn", @"error"]];
               pb.controlSize = NSControlSizeSmall;
               pb.target = self;
               pb.action = @selector(logLevelChanged:);
               self.logLevelPopup = pb;
               pb;
           })],
    ]];
    grid.translatesAutoresizingMaskIntoConstraints = NO;
    grid.columnSpacing = 12;
    grid.rowSpacing = 10;
    [v addSubview:grid];

    [NSLayoutConstraint activateConstraints:@[
        [grid.topAnchor constraintEqualToAnchor:v.topAnchor constant:16],
        [grid.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:16],
    ]];

    item.view = v;
    return item;
}

#pragma mark - Desktop Lyrics Tab

- (NSTabViewItem *)deskLyricsTab {
    NSTabViewItem *item = [[NSTabViewItem alloc] initWithIdentifier:@"deskLyrics"];
    item.label = @"桌面歌词";

    NSView *v = [[NSView alloc] initWithFrame:NSZeroRect];
    NSView *lastRow = nil;

    auto addGridRow = [&](NSView *label, NSView *control) {
        NSGridView *row = [NSGridView gridViewWithViews:@[@[label, control]]];
        row.translatesAutoresizingMaskIntoConstraints = NO;
        row.columnSpacing = 12;
        [v addSubview:row];
        [NSLayoutConstraint activateConstraints:@[
            [row.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:16],
            [row.trailingAnchor constraintLessThanOrEqualToAnchor:v.trailingAnchor constant:-16],
            lastRow ? [row.topAnchor constraintEqualToAnchor:lastRow.bottomAnchor constant:10]
                    : [row.topAnchor constraintEqualToAnchor:v.topAnchor constant:16],
        ]];
        lastRow = row;
        return row;
    };

    // 启用
    NSButton *enabledCheck = [NSButton checkboxWithTitle:@"启用桌面歌词" target:self action:@selector(deskEnabledChanged:)];
    self.deskEnabledCheck = enabledCheck;
    addGridRow([NSTextField labelWithString:@""], enabledCheck);

    // 仅后台显示
    NSButton *bgCheck = [NSButton checkboxWithTitle:@"仅 foobar2000 后台时显示" target:self action:@selector(deskBackgroundChanged:)];
    self.deskBackgroundCheck = bgCheck;
    addGridRow([NSTextField labelWithString:@""], bgCheck);

    // 显示标题栏
    NSButton *titleCheck = [NSButton checkboxWithTitle:@"显示标题栏（歌名 — 艺术家）" target:self action:@selector(deskShowTitleChanged:)];
    self.deskShowTitleCheck = titleCheck;
    addGridRow([NSTextField labelWithString:@""], titleCheck);

    // 字号
    NSTextField *fontSizeF = [[NSTextField alloc] initWithFrame:NSZeroRect];
    fontSizeF.controlSize = NSControlSizeSmall;
    fontSizeF.target = self;
    fontSizeF.action = @selector(deskFontSizeChanged:);
    self.deskFontSizeField = fontSizeF;
    addGridRow([NSTextField labelWithString:@"字号："], fontSizeF);

    // 常规色
    NSColorWell *normalWell = [NSColorWell new];
    normalWell.target = self;
    normalWell.action = @selector(deskColorChanged:);
    self.deskNormalColorWell = normalWell;
    addGridRow([NSTextField labelWithString:@"常规色："], normalWell);

    // 高亮色
    NSColorWell *hlWell = [NSColorWell new];
    hlWell.target = self;
    hlWell.action = @selector(deskColorChanged:);
    self.deskHighlightColorWell = hlWell;
    addGridRow([NSTextField labelWithString:@"高亮色："], hlWell);

    // 对齐
    NSPopUpButton *alignPop = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [alignPop addItemsWithTitles:@[@"居中", @"左对齐", @"右对齐"]];
    alignPop.target = self;
    alignPop.action = @selector(deskAlignmentChanged:);
    self.deskAlignmentPopup = alignPop;
    addGridRow([NSTextField labelWithString:@"对齐："], alignPop);

    // 行距
    NSSlider *spSlider = [[NSSlider alloc] initWithFrame:NSZeroRect];
    spSlider.minValue = 0;
    spSlider.maxValue = 20;
    spSlider.target = self;
    spSlider.action = @selector(deskLineSpacingChanged:);
    self.deskLineSpacingSlider = spSlider;

    NSTextField *spLbl = [NSTextField labelWithString:@"0"];
    self.deskLineSpacingLabel = spLbl;

    NSView *spRow = [[NSView alloc] initWithFrame:NSZeroRect];
    spSlider.translatesAutoresizingMaskIntoConstraints = NO;
    spLbl.translatesAutoresizingMaskIntoConstraints = NO;
    [spRow addSubview:spSlider];
    [spRow addSubview:spLbl];
    [NSLayoutConstraint activateConstraints:@[
        [spSlider.leadingAnchor constraintEqualToAnchor:spRow.leadingAnchor],
        [spSlider.centerYAnchor constraintEqualToAnchor:spRow.centerYAnchor],
        [spSlider.widthAnchor constraintEqualToConstant:160],
        [spLbl.leadingAnchor constraintEqualToAnchor:spSlider.trailingAnchor constant:8],
        [spLbl.centerYAnchor constraintEqualToAnchor:spRow.centerYAnchor],
        [spRow.heightAnchor constraintEqualToConstant:24],
    ]];
    addGridRow([NSTextField labelWithString:@"行距："], spRow);

    // 显示行数
    NSTextField *maxLinesF = [[NSTextField alloc] initWithFrame:NSZeroRect];
    maxLinesF.controlSize = NSControlSizeSmall;
    maxLinesF.target = self;
    maxLinesF.action = @selector(deskMaxLinesChanged:);
    self.deskMaxLinesField = maxLinesF;
    addGridRow([NSTextField labelWithString:@"显示行数："], maxLinesF);

    // 窗口宽度
    NSTextField *winWF = [[NSTextField alloc] initWithFrame:NSZeroRect];
    winWF.controlSize = NSControlSizeSmall;
    winWF.target = self;
    winWF.action = @selector(deskWindowWidthChanged:);
    self.deskWindowWidthField = winWF;
    addGridRow([NSTextField labelWithString:@"窗口宽度："], winWF);

    // 窗口高度
    NSTextField *winHF = [[NSTextField alloc] initWithFrame:NSZeroRect];
    winHF.controlSize = NSControlSizeSmall;
    winHF.target = self;
    winHF.action = @selector(deskWindowHeightChanged:);
    self.deskWindowHeightField = winHF;
    addGridRow([NSTextField labelWithString:@"窗口高度："], winHF);

    item.view = v;
    return item;
}

#pragma mark - Desktop Lyrics actions

- (void)deskEnabledChanged:(NSButton *)sender {
    _config.deskLyrics.enabled = (sender.state == NSControlStateValueOn);
    [self saveConfig];
}

- (void)deskBackgroundChanged:(NSButton *)sender {
    _config.deskLyrics.showOnlyInBackground = (sender.state == NSControlStateValueOn);
    [self saveConfig];
}

- (void)deskShowTitleChanged:(NSButton *)sender {
    _config.deskLyrics.showTitle = (sender.state == NSControlStateValueOn);
    [self saveConfig];
}

- (void)deskFontSizeChanged:(NSTextField *)sender {
    double v = [sender.stringValue doubleValue];
    if (v >= 8 && v <= 120) _config.deskLyrics.fontSize = v;
    [self saveConfig];
}

- (void)deskColorChanged:(NSColorWell *)sender {
    if (sender == _deskNormalColorWell)
        _config.deskLyrics.normalColor = [self hexFromColor:_deskNormalColorWell.color].UTF8String;
    else
        _config.deskLyrics.highlightColor = [self hexFromColor:_deskHighlightColorWell.color].UTF8String;
    [self saveConfig];
}

- (void)deskAlignmentChanged:(NSPopUpButton *)sender {
    NSInteger idx = sender.indexOfSelectedItem;
    _config.deskLyrics.alignment = (idx == 1) ? "left" : (idx == 2) ? "right" : "center";
    [self saveConfig];
}

- (void)deskLineSpacingChanged:(NSSlider *)sender {
    _config.deskLyrics.lineSpacing = sender.doubleValue;
    _deskLineSpacingLabel.stringValue = [NSString stringWithFormat:@"%.0f", sender.doubleValue];
    [self saveConfig];
}

- (void)deskMaxLinesChanged:(NSTextField *)sender {
    int v = [sender.stringValue intValue];
    if (v < 3) v = 3;
    if (v > 7) v = 7;
    _config.deskLyrics.maxLines = v;
    sender.stringValue = [NSString stringWithFormat:@"%d", v];
    [self saveConfig];
}

- (void)deskWindowWidthChanged:(NSTextField *)sender {
    double v = [sender.stringValue doubleValue];
    if (v < kMinPanelWidth) v = kMinPanelWidth;
    _config.deskLyrics.windowWidth = v;
    [self saveConfig];
}

- (void)deskWindowHeightChanged:(NSTextField *)sender {
    double v = [sender.stringValue doubleValue];
    if (v < kMinPanelHeight) v = kMinPanelHeight;
    _config.deskLyrics.windowHeight = v;
    [self saveConfig];
}

#pragma mark - Populate from config

- (void)viewWillAppear {
    [super viewWillAppear];
    [self populateFromConfig];
}

- (void)populateFromConfig {
    const auto& c = _config;
    [_sourceTable reloadData];

    const auto& d = c.display;
    _fontLabel.stringValue = [NSString stringWithFormat:@"%@ %.0fpt",
        [NSString stringWithUTF8String:d.fontName.c_str()], d.fontSize];
    _normalColorWell.color = [self colorFromHex:d.normalColor];
    _highlightColorWell.color = [self colorFromHex:d.highlightColor];

    NSInteger alignIdx = 0;
    if (d.alignment == "left") alignIdx = 1;
    else if (d.alignment == "right") alignIdx = 2;
    [_alignmentPopup selectItemAtIndex:alignIdx];

    _lineSpacingSlider.doubleValue = d.lineSpacing;
    _lineSpacingLabel.stringValue = [NSString stringWithFormat:@"%.0f", d.lineSpacing];

    _defaultOffsetField.stringValue = [NSString stringWithFormat:@"%lld", c.defaultOffsetMs];
    _httpTimeoutField.stringValue = [NSString stringWithFormat:@"%d", c.httpTimeoutSec];
    _maxFailuresField.stringValue = [NSString stringWithFormat:@"%d", c.maxConsecutiveFailures];

    NSInteger logIdx = 0;
    if (c.logLevel == "info") logIdx = 1;
    else if (c.logLevel == "warn") logIdx = 2;
    else if (c.logLevel == "error") logIdx = 3;
    [_logLevelPopup selectItemAtIndex:logIdx];

    const auto& dl = c.deskLyrics;
    _deskEnabledCheck.state = dl.enabled ? NSControlStateValueOn : NSControlStateValueOff;
    _deskBackgroundCheck.state = dl.showOnlyInBackground ? NSControlStateValueOn : NSControlStateValueOff;
    _deskShowTitleCheck.state = dl.showTitle ? NSControlStateValueOn : NSControlStateValueOff;
    _deskFontSizeField.stringValue = [NSString stringWithFormat:@"%.0f", dl.fontSize];
    _deskNormalColorWell.color = [self colorFromHex:dl.normalColor];
    _deskHighlightColorWell.color = [self colorFromHex:dl.highlightColor];

    NSInteger dlAlign = 0;
    if (dl.alignment == "left") dlAlign = 1;
    else if (dl.alignment == "right") dlAlign = 2;
    [_deskAlignmentPopup selectItemAtIndex:dlAlign];

    _deskLineSpacingSlider.doubleValue = dl.lineSpacing;
    _deskLineSpacingLabel.stringValue = [NSString stringWithFormat:@"%.0f", dl.lineSpacing];

    _deskMaxLinesField.stringValue = [NSString stringWithFormat:@"%d", dl.maxLines];
    _deskWindowWidthField.stringValue = [NSString stringWithFormat:@"%.0f", dl.windowWidth];
    _deskWindowHeightField.stringValue = [NSString stringWithFormat:@"%.0f", dl.windowHeight];
}

#pragma mark - NSTableViewDataSource / Delegate (Sources tab)

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    return (NSInteger)_config.sources.size();
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)col row:(NSInteger)row {
    if (row < 0 || row >= (NSInteger)_config.sources.size()) return nil;
    const auto& src = _config.sources[row];

    if ([col.identifier isEqualToString:@"name"]) {
        NSString *cellId = @"srcName";
        NSTableCellView *cell = [tableView makeViewWithIdentifier:cellId owner:self];
        if (cell == nil) {
            cell = [[NSTableCellView alloc] initWithFrame:NSZeroRect];
            cell.identifier = cellId;
            NSTextField *tf = [NSTextField labelWithString:@""];
            tf.font = [NSFont systemFontOfSize:12];
            tf.translatesAutoresizingMaskIntoConstraints = NO;
            [cell addSubview:tf];
            [NSLayoutConstraint activateConstraints:@[
                [tf.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:4],
                [tf.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
            ]];
            cell.textField = tf;
        }
        cell.textField.stringValue = [self displayNameForSource:src.key];
        return cell;
    } else {
        NSString *cellId = @"srcEnabled";
        NSButton *cb = [tableView makeViewWithIdentifier:cellId owner:self];
        if (cb == nil) {
            cb = [NSButton checkboxWithTitle:@"" target:self action:@selector(sourceToggle:)];
            cb.identifier = cellId;
        }
        cb.state = src.enabled ? NSControlStateValueOn : NSControlStateValueOff;
        cb.tag = row;
        return cb;
    }
}

- (void)sourceToggle:(NSButton *)sender {
    NSInteger row = sender.tag;
    if (row < 0 || row >= (NSInteger)_config.sources.size()) return;
    _config.sources[row].enabled = (sender.state == NSControlStateValueOn);
    [self saveConfig];
}

// 拖拽重排
- (id<NSPasteboardWriting>)tableView:(NSTableView *)tableView pasteboardWriterForRow:(NSInteger)row {
    NSPasteboardItem *item = [[NSPasteboardItem alloc] init];
    [item setString:[NSString stringWithFormat:@"%ld", (long)row] forType:kDragDropType];
    return item;
}

- (NSDragOperation)tableView:(NSTableView *)tableView validateDrop:(id<NSDraggingInfo>)info
                 proposedRow:(NSInteger)row proposedDropOperation:(NSTableViewDropOperation)op {
    if (op == NSTableViewDropAbove) return NSDragOperationMove;
    return NSDragOperationNone;
}

- (BOOL)tableView:(NSTableView *)tableView acceptDrop:(id<NSDraggingInfo>)info
              row:(NSInteger)row dropOperation:(NSTableViewDropOperation)op {
    NSPasteboard *pb = [info draggingPasteboard];
    NSString *str = [pb stringForType:kDragDropType];
    if (str == nil) return NO;
    NSInteger from = [str integerValue];
    if (from < 0 || from >= (NSInteger)_config.sources.size()) return NO;

    auto src = _config.sources[from];
    _config.sources.erase(_config.sources.begin() + from);
    if (row > from) --row;
    _config.sources.insert(_config.sources.begin() + row, src);
    [tableView reloadData];
    [self saveConfig];
    return YES;
}

#pragma mark - Display actions

- (void)chooseFont:(id)sender {
    NSFontManager *fm = [NSFontManager sharedFontManager];
    NSFont *current = [NSFont fontWithName:[NSString stringWithUTF8String:_config.display.fontName.c_str()]
                                      size:_config.display.fontSize];
    if (current == nil) current = [NSFont systemFontOfSize:14];
    [fm setSelectedFont:current isMultiple:NO];
    [fm setTarget:self];
    [fm setAction:@selector(fontChanged:)];
    [fm orderFrontFontPanel:sender];
}

- (void)fontChanged:(id)sender {
    NSFont *f = [sender convertFont:[NSFont systemFontOfSize:14]];
    _config.display.fontName = f.fontName.UTF8String;
    _config.display.fontSize = f.pointSize;
    _fontLabel.stringValue = [NSString stringWithFormat:@"%@ %.0fpt", f.fontName, f.pointSize];
    [self saveConfig];
}

- (void)colorChanged:(id)sender {
    if (sender == _normalColorWell)
        _config.display.normalColor = [self hexFromColor:_normalColorWell.color].UTF8String;
    else
        _config.display.highlightColor = [self hexFromColor:_highlightColorWell.color].UTF8String;
    [self saveConfig];
}

- (void)alignmentChanged:(NSPopUpButton *)sender {
    NSInteger idx = sender.indexOfSelectedItem;
    _config.display.alignment = (idx == 1) ? "left" : (idx == 2) ? "right" : "center";
    [self saveConfig];
}

- (void)lineSpacingChanged:(NSSlider *)sender {
    _config.display.lineSpacing = sender.doubleValue;
    _lineSpacingLabel.stringValue = [NSString stringWithFormat:@"%.0f", sender.doubleValue];
    [self saveConfig];
}

#pragma mark - Advanced actions

- (void)logLevelChanged:(NSPopUpButton *)sender {
    _config.logLevel = sender.titleOfSelectedItem.UTF8String;
    [self saveConfig];
}

- (void)viewWillDisappear {
    [super viewWillDisappear];
    // 输入框的值在失去焦点时可能未提交；这里主动同步一次
    _config.defaultOffsetMs = [_defaultOffsetField.stringValue longLongValue];
    _config.httpTimeoutSec = [_httpTimeoutField.stringValue intValue];
    if (_config.httpTimeoutSec < 1) _config.httpTimeoutSec = 1;
    _config.maxConsecutiveFailures = [_maxFailuresField.stringValue intValue];
    if (_config.maxConsecutiveFailures < 1) _config.maxConsecutiveFailures = 1;
    [self saveConfig];
}

#pragma mark - Helpers

- (NSColor *)colorFromHex:(const std::string&)hex {
    if (hex.size() < 7 || hex[0] != '#') return [NSColor blackColor];
    unsigned int r = 0, g = 0, b = 0;
    sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
    return [NSColor colorWithRed:r/255.0 green:g/255.0 blue:b/255.0 alpha:1.0];
}

- (NSString *)hexFromColor:(NSColor *)c {
    c = [c colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    return [NSString stringWithFormat:@"#%02X%02X%02X",
        (int)(c.redComponent * 255), (int)(c.greenComponent * 255), (int)(c.blueComponent * 255)];
}

- (void)saveConfig {
    openlyrics::ConfigAdapter().save(_config);
    [[DesktopLyricsController sharedController] reloadConfig];
    FB2K_console_print("foo_openlyrics: config saved");
}

@end
