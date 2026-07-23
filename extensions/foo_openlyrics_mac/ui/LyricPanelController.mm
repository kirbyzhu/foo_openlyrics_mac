// LyricPanelController.mm
// foo_openlyrics_mac —— Plan 2-5 歌词面板控制器；
// Plan 6 Task 3-4：新增编辑模式（NSTextView 覆层 + 解析保存）+ 配置接线（动态源顺序、
// 显示配置、超时、默认 offset）。
#import "LyricPanelController.h"
#import "stdafx.h"

#import "LyricView.h"
#import "TagIOAdapter.h"
#import "FileSystemAdapter.h"
#import "HttpAdapter.h"
#import "CryptoAdapter.h"
#import "ConfigAdapter.h"

#include "sources/TagSource.h"
#include "sources/LocalFileSource.h"
#include "sources/LrcLibProvider.h"
#include "sources/NetEaseProvider.h"
#include "sources/QQMusicProvider.h"
#include "pipeline/SearchPipeline.h"
#include "store/LyricStore.h"
#include "sync/SyncEngine.h"
#include "model/LyricData.h"
#include "model/SearchResult.h"
#include "parser/LrcParser.h"
#include "parser/LrcSerializer.h"
#include "config/AppConfig.h"

static NSString *const kPlaceholderText = @"未在播放";
static const NSTimeInterval kSyncTickInterval = 0.06;
static const double kOffsetStep = 0.1;
static const double kOffsetMin = -30.0;
static const double kOffsetMax = 30.0;

@interface LyricPanelController () <NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic, strong) NSSearchField *searchField;
@property(nonatomic, strong) NSTextField *statusLabel;
@property(nonatomic, strong) LyricView *lyricView;
@property(nonatomic, strong) NSTimer *syncTimer;
@property(nonatomic, assign) NSInteger trackRequestToken;

// offset
@property(nonatomic, strong) NSView *offsetContainer;
@property(nonatomic, strong) NSStepper *offsetStepper;
@property(nonatomic, strong) NSTextField *offsetLabel;
@property(nonatomic, strong) NSButton *applyOffsetBtn;

// 编辑器
@property(nonatomic, strong) NSButton *editBtn;
@property(nonatomic, strong) NSScrollView *editScrollView;
@property(nonatomic, strong) NSTextView *editTextView;
@property(nonatomic, strong) NSButton *editDoneBtn;
@property(nonatomic, strong) NSButton *editCancelBtn;

// 搜索
@property(nonatomic, strong) NSPopover *searchPopover;
@property(nonatomic, strong) NSTableView *searchTableView;
@property(nonatomic, copy) NSArray<NSDictionary *> *searchResults;
@end

@implementation LyricPanelController {
    openlyrics::LyricData _currentLyricData;
    int64_t _currentExtraOffsetMs;
    std::string _currentSourceLabel;
    int _lrclibFailures;
    int _neteaseFailures;
    int _qqmusicFailures;
    openlyrics::AppConfig _config;
}

- (void)loadView {
    _config = openlyrics::ConfigAdapter().load();

    // 应用全局配置
    openlyrics::HttpAdapter::setGlobalTimeout(_config.httpTimeoutSec);

    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 200)];

    // 搜索框
    NSSearchField *search = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    search.placeholderString = @"搜索 LrcLib…";
    search.translatesAutoresizingMaskIntoConstraints = NO;
    search.target = self;
    search.action = @selector(searchFieldAction:);
    [root addSubview:search];

    // 状态行 + offset/编辑控件
    NSView *statusRow = [[NSView alloc] initWithFrame:NSZeroRect];
    statusRow.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:statusRow];

    NSTextField *status = [NSTextField labelWithString:kPlaceholderText];
    status.font = [NSFont systemFontOfSize:11];
    status.textColor = [NSColor secondaryLabelColor];
    status.lineBreakMode = NSLineBreakByTruncatingTail;
    status.translatesAutoresizingMaskIntoConstraints = NO;
    [statusRow addSubview:status];

    // offset 控件组
    NSView *offsetBox = [[NSView alloc] initWithFrame:NSZeroRect];
    offsetBox.translatesAutoresizingMaskIntoConstraints = NO;
    offsetBox.hidden = YES;

    NSStepper *stepper = [[NSStepper alloc] initWithFrame:NSZeroRect];
    stepper.minValue = kOffsetMin;
    stepper.maxValue = kOffsetMax;
    stepper.increment = kOffsetStep;
    stepper.valueWraps = NO;
    stepper.translatesAutoresizingMaskIntoConstraints = NO;
    stepper.target = self;
    stepper.action = @selector(offsetStepperAction:);

    NSTextField *offsetLbl = [NSTextField labelWithString:@"+0.00s"];
    offsetLbl.font = [NSFont monospacedDigitSystemFontOfSize:10 weight:NSFontWeightRegular];
    offsetLbl.alignment = NSTextAlignmentRight;
    offsetLbl.translatesAutoresizingMaskIntoConstraints = NO;

    NSButton *applyBtn = [NSButton buttonWithTitle:@"应用" target:self action:@selector(applyOffsetAction:)];
    applyBtn.font = [NSFont systemFontOfSize:10];
    applyBtn.controlSize = NSControlSizeSmall;
    applyBtn.translatesAutoresizingMaskIntoConstraints = NO;

    NSButton *editBtn = [NSButton buttonWithTitle:@"编辑" target:self action:@selector(toggleEditMode:)];
    editBtn.font = [NSFont systemFontOfSize:10];
    editBtn.controlSize = NSControlSizeSmall;
    editBtn.translatesAutoresizingMaskIntoConstraints = NO;

    [offsetBox addSubview:stepper];
    [offsetBox addSubview:offsetLbl];
    [offsetBox addSubview:applyBtn];
    [offsetBox addSubview:editBtn];
    [statusRow addSubview:offsetBox];

    // LyricView
    LyricView *lyricView = [[LyricView alloc] initWithFrame:NSZeroRect];
    lyricView.translatesAutoresizingMaskIntoConstraints = NO;
    [lyricView applyDisplayConfig:_config.display];
    [root addSubview:lyricView];

    // 编辑模式 NSTextView（初始隐藏）
    NSScrollView *editScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    editScroll.hasVerticalScroller = YES;
    editScroll.translatesAutoresizingMaskIntoConstraints = NO;
    editScroll.hidden = YES;

    NSTextView *editText = [[NSTextView alloc] initWithFrame:NSZeroRect];
    editText.font = [NSFont fontWithName:@"Monaco" size:12] ?: [NSFont systemFontOfSize:12];
    editText.automaticQuoteSubstitutionEnabled = NO;
    editText.automaticDashSubstitutionEnabled = NO;
    editText.continuousSpellCheckingEnabled = NO;
    editText.grammarCheckingEnabled = NO;
    editText.translatesAutoresizingMaskIntoConstraints = NO;
    editScroll.documentView = editText;

    NSButton *doneBtn = [NSButton buttonWithTitle:@"完成" target:self action:@selector(editDoneAction:)];
    doneBtn.font = [NSFont systemFontOfSize:11];
    doneBtn.controlSize = NSControlSizeSmall;
    doneBtn.translatesAutoresizingMaskIntoConstraints = NO;
    doneBtn.hidden = YES;

    NSButton *cancelBtn = [NSButton buttonWithTitle:@"取消" target:self action:@selector(editCancelAction:)];
    cancelBtn.font = [NSFont systemFontOfSize:11];
    cancelBtn.controlSize = NSControlSizeSmall;
    cancelBtn.translatesAutoresizingMaskIntoConstraints = NO;
    cancelBtn.hidden = YES;

    [root addSubview:editScroll];
    [root addSubview:doneBtn];
    [root addSubview:cancelBtn];

    // 布局约束
    [NSLayoutConstraint activateConstraints:@[
        [search.topAnchor constraintEqualToAnchor:root.topAnchor constant:4],
        [search.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:4],
        [search.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-4],

        [statusRow.topAnchor constraintEqualToAnchor:search.bottomAnchor constant:3],
        [statusRow.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:8],
        [statusRow.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-8],
        [statusRow.heightAnchor constraintEqualToConstant:18],

        [status.leadingAnchor constraintEqualToAnchor:statusRow.leadingAnchor],
        [status.centerYAnchor constraintEqualToAnchor:statusRow.centerYAnchor],

        [offsetBox.trailingAnchor constraintEqualToAnchor:statusRow.trailingAnchor],
        [offsetBox.centerYAnchor constraintEqualToAnchor:statusRow.centerYAnchor],
        [offsetBox.heightAnchor constraintEqualToConstant:18],

        [stepper.leadingAnchor constraintEqualToAnchor:offsetBox.leadingAnchor],
        [stepper.centerYAnchor constraintEqualToAnchor:offsetBox.centerYAnchor],

        [offsetLbl.leadingAnchor constraintEqualToAnchor:stepper.trailingAnchor constant:3],
        [offsetLbl.centerYAnchor constraintEqualToAnchor:offsetBox.centerYAnchor],
        [offsetLbl.widthAnchor constraintEqualToConstant:52],

        [applyBtn.leadingAnchor constraintEqualToAnchor:offsetLbl.trailingAnchor constant:4],
        [applyBtn.centerYAnchor constraintEqualToAnchor:offsetBox.centerYAnchor],

        [editBtn.leadingAnchor constraintEqualToAnchor:applyBtn.trailingAnchor constant:4],
        [editBtn.trailingAnchor constraintEqualToAnchor:offsetBox.trailingAnchor],
        [editBtn.centerYAnchor constraintEqualToAnchor:offsetBox.centerYAnchor],

        [lyricView.topAnchor constraintEqualToAnchor:statusRow.bottomAnchor constant:2],
        [lyricView.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [lyricView.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [lyricView.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],

        [editScroll.topAnchor constraintEqualToAnchor:lyricView.topAnchor],
        [editScroll.leadingAnchor constraintEqualToAnchor:lyricView.leadingAnchor],
        [editScroll.trailingAnchor constraintEqualToAnchor:lyricView.trailingAnchor],

        [doneBtn.topAnchor constraintEqualToAnchor:editScroll.bottomAnchor constant:4],
        [doneBtn.trailingAnchor constraintEqualToAnchor:cancelBtn.leadingAnchor constant:-8],
        [doneBtn.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-4],

        [cancelBtn.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-8],
        [cancelBtn.centerYAnchor constraintEqualToAnchor:doneBtn.centerYAnchor],
    ]];

    // 搜索 popover（与 plan5 保持一致的实现）
    _searchPopover = [[NSPopover alloc] init];
    _searchPopover.behavior = NSPopoverBehaviorTransient;
    _searchPopover.animates = YES;

    NSTableView *tv = [[NSTableView alloc] initWithFrame:NSZeroRect];
    NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:@"result"];
    col.title = @""; col.width = 260;
    [tv addTableColumn:col];
    tv.headerView = nil; tv.rowHeight = 36;
    tv.dataSource = self; tv.delegate = self;
    tv.target = self; tv.doubleAction = @selector(searchRowDoubleClicked:);
    _searchTableView = tv;

    NSScrollView *popScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 280, 200)];
    popScroll.documentView = tv; popScroll.hasVerticalScroller = YES;
    NSViewController *popVC = [[NSViewController alloc] init];
    popVC.view = popScroll;
    _searchPopover.contentViewController = popVC;

    self.searchField = search;
    self.statusLabel = status;
    self.lyricView = lyricView;
    self.offsetContainer = offsetBox;
    self.offsetStepper = stepper;
    self.offsetLabel = offsetLbl;
    self.applyOffsetBtn = applyBtn;
    self.editBtn = editBtn;
    self.editScrollView = editScroll;
    self.editTextView = editText;
    self.editDoneBtn = doneBtn;
    self.editCancelBtn = cancelBtn;
    self.view = root;
}

#pragma mark - 编辑模式

- (void)toggleEditMode:(id)sender {
    if (_editTextView.hidden) {
        // 进入编辑模式
        std::string text = _currentLyricData.sourceText.empty()
            ? openlyrics::LrcSerializer::serialize(_currentLyricData)
            : _currentLyricData.sourceText;
        _editTextView.string = [NSString stringWithUTF8String:text.c_str()];
        _lyricView.hidden = YES;
        _editScrollView.hidden = NO;
        _editDoneBtn.hidden = NO;
        _editCancelBtn.hidden = NO;
        _editBtn.hidden = YES;
        _offsetContainer.hidden = YES;
        [self.view.window makeFirstResponder:_editTextView];
    }
}

- (void)exitEditMode {
    _lyricView.hidden = NO;
    _editScrollView.hidden = YES;
    _editDoneBtn.hidden = YES;
    _editCancelBtn.hidden = YES;
    _editBtn.hidden = NO;
    _offsetContainer.hidden = _currentLyricData.lines.empty();
}

- (void)editDoneAction:(id)sender {
    std::string text = _editTextView.string.UTF8String ?: "";
    openlyrics::LyricData parsed = openlyrics::LrcParser::parse(text);
    if (parsed.lines.empty()) return;  // 解析无有效行，不保存

    parsed.sourceText = text;
    _currentLyricData = parsed;
    _currentExtraOffsetMs = 0;
    [self.lyricView setLyricData:parsed];
    [self exitEditMode];
    [self updateOffsetUI];

    // 覆写 .lrc
    PlaybackHub *hub = [PlaybackHub sharedHub];
    if ([hub hasTrack]) {
        openlyrics::TrackMeta meta = [hub currentTrack];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            openlyrics::FileSystemAdapter fs;
            openlyrics::LyricStore store(fs);
            store.forceSave(meta, parsed);
        });
    }
}

- (void)editCancelAction:(id)sender {
    [self exitEditMode];
}

#pragma mark - 搜索（同 plan5）

- (void)searchFieldAction:(NSSearchField *)sender {
    NSString *query = [sender.stringValue stringByTrimmingCharactersInSet:
                       [NSCharacterSet whitespaceCharacterSet]];
    if (query.length == 0) return;
    [_searchPopover close];
    sender.placeholderString = @"搜索中…";

    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        openlyrics::HttpAdapter http;
        openlyrics::LrcLibProvider lrcLib(http);
        std::vector<openlyrics::SearchResult> results;
        lrcLib.search(query.UTF8String, results);

        NSMutableArray<NSDictionary *> *arr = [NSMutableArray arrayWithCapacity:results.size()];
        for (const auto& r : results) {
            [arr addObject:@{
                @"id": [NSString stringWithUTF8String:r.id.c_str()], @"trackName": [NSString stringWithUTF8String:r.trackName.c_str()],
                @"artistName": [NSString stringWithUTF8String:r.artistName.c_str()],
                @"albumName": [NSString stringWithUTF8String:r.albumName.c_str()],
            }];
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) return;
            strongSelf.searchField.placeholderString = @"搜索 LrcLib…";
            strongSelf.searchResults = arr;
            [strongSelf.searchTableView reloadData];
            if (arr.count > 0) {
                NSViewController *vc = strongSelf.searchPopover.contentViewController;
                vc.view.frame = NSMakeRect(0, 0, 280,
                    MIN(arr.count * strongSelf.searchTableView.rowHeight + 4, 200));
                [strongSelf.searchPopover showRelativeToRect:strongSelf.searchField.bounds
                                                      ofView:strongSelf.searchField
                                               preferredEdge:NSRectEdgeMaxY];
            }
        });
    });
}

- (void)searchRowDoubleClicked:(id)sender {
    NSInteger row = _searchTableView.clickedRow;
    if (row < 0 || row >= (NSInteger)_searchResults.count) return;
    int lyricId = [_searchResults[row][@"id"] intValue];
    [_searchPopover close];
    self.searchField.stringValue = @"";

    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) return;
    openlyrics::TrackMeta meta = [hub currentTrack];
    NSString *title = meta.title.empty() ? @"(未知曲目)"
        : [NSString stringWithUTF8String:meta.title.c_str()];
    self.statusLabel.stringValue = [NSString stringWithFormat:@"%@ · 获取歌词中…", title];

    const NSInteger requestToken = self.trackRequestToken;
    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        openlyrics::HttpAdapter http;
        openlyrics::LrcLibProvider lrcLib(http);
        openlyrics::LyricData data;
        if (!lrcLib.fetchById(lyricId, data)) {
            dispatch_async(dispatch_get_main_queue(), ^{
                __typeof__(self) strongSelf = weakSelf;
                if (strongSelf == nil || strongSelf.trackRequestToken != requestToken) return;
                strongSelf.statusLabel.stringValue =
                    [NSString stringWithFormat:@"%@ · 获取失败", title];
            });
            return;
        }
        openlyrics::FileSystemAdapter fs;
        openlyrics::LyricStore store(fs);
        store.save(meta, data);

        dispatch_async(dispatch_get_main_queue(), ^{
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil || strongSelf.trackRequestToken != requestToken) return;
            strongSelf->_currentLyricData = data;
            strongSelf->_currentExtraOffsetMs = 0;
            strongSelf->_currentSourceLabel = "search";
            [strongSelf.lyricView setLyricData:data];
            strongSelf.statusLabel.stringValue = title;
            [strongSelf updateOffsetUI];
            strongSelf.offsetContainer.hidden = data.lines.empty();
        });
    });
}

#pragma mark - NSTableViewDataSource

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    return (NSInteger)_searchResults.count;
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)col row:(NSInteger)row {
    if (row < 0 || row >= (NSInteger)_searchResults.count) return nil;
    NSDictionary *item = _searchResults[row];
    NSString *cellId = @"searchCell";
    NSTableCellView *cell = [tableView makeViewWithIdentifier:cellId owner:self];
    if (cell == nil) {
        cell = [[NSTableCellView alloc] initWithFrame:NSZeroRect];
        cell.identifier = cellId;
        NSTextField *tf = [NSTextField labelWithString:@""];
        tf.font = [NSFont systemFontOfSize:11];
        tf.lineBreakMode = NSLineBreakByTruncatingTail;
        tf.translatesAutoresizingMaskIntoConstraints = NO;
        [cell addSubview:tf];
        [NSLayoutConstraint activateConstraints:@[
            [tf.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:4],
            [tf.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-4],
            [tf.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        ]];
        cell.textField = tf;
    }
    cell.textField.stringValue = [NSString stringWithFormat:@"%@  —  %@",
        item[@"trackName"], item[@"artistName"]];
    return cell;
}

#pragma mark - Offset

- (void)offsetStepperAction:(NSStepper *)sender {
    _currentExtraOffsetMs = static_cast<int64_t>(sender.doubleValue * 1000.0);
    [self updateOffsetLabel];
}

- (void)applyOffsetAction:(NSButton *)sender {
    if (_currentExtraOffsetMs == 0) return;
    if (_currentLyricData.sourceText.empty()) return;

    int64_t newTotalOffsetMs = _currentLyricData.offsetMs + _currentExtraOffsetMs;
    std::string newSourceText = _currentLyricData.sourceText;

    size_t offsetPos = newSourceText.find("[offset:");
    if (offsetPos != std::string::npos) {
        size_t endPos = newSourceText.find(']', offsetPos);
        size_t lineEnd = newSourceText.find('\n', offsetPos);
        if (endPos != std::string::npos) {
            size_t eraseEnd = (lineEnd != std::string::npos) ? lineEnd : endPos + 1;
            newSourceText.erase(offsetPos, eraseEnd - offsetPos);
            if (lineEnd == std::string::npos && offsetPos > 0) {
                newSourceText.insert(offsetPos, "\n"); offsetPos += 1;
            }
        }
    }

    std::string offsetLine = "[offset:" + std::to_string(newTotalOffsetMs) + "]";
    if (offsetPos == std::string::npos) {
        size_t lastTag = std::string::npos;
        const char* idTags[] = {"[ti:", "[ar:", "[al:", "[by:", "[length:"};
        for (const char* tag : idTags) {
            size_t p = newSourceText.rfind(tag);
            if (p != std::string::npos) lastTag = std::max(lastTag, p);
        }
        if (lastTag != std::string::npos) {
            size_t eol = newSourceText.find('\n', lastTag);
            if (eol != std::string::npos)
                newSourceText.insert(eol + 1, offsetLine + "\n");
            else
                newSourceText += "\n" + offsetLine;
        } else {
            newSourceText = offsetLine + "\n" + newSourceText;
        }
    } else {
        newSourceText.insert(offsetPos, offsetLine);
    }

    _currentLyricData = openlyrics::LrcParser::parse(newSourceText);
    _currentLyricData.sourceText = newSourceText;
    _currentExtraOffsetMs = 0;

    PlaybackHub *hub = [PlaybackHub sharedHub];
    if ([hub hasTrack]) {
        openlyrics::TrackMeta meta = [hub currentTrack];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            openlyrics::FileSystemAdapter fs;
            openlyrics::LyricStore store(fs);
            store.forceSave(meta, self->_currentLyricData);
        });
    }

    [self.lyricView setLyricData:_currentLyricData];
    [self updateOffsetUI];
}

- (void)updateOffsetLabel {
    double sec = _currentExtraOffsetMs / 1000.0;
    self.offsetLabel.stringValue = [NSString stringWithFormat:@"%+.2fs", sec];
}

- (void)updateOffsetUI {
    _offsetStepper.doubleValue = _currentExtraOffsetMs / 1000.0;
    [self updateOffsetLabel];
}

#pragma mark - 生命周期

- (void)viewWillAppear {
    [super viewWillAppear];
    _config = openlyrics::ConfigAdapter().load();
    openlyrics::HttpAdapter::setGlobalTimeout(_config.httpTimeoutSec);
    [self.lyricView applyDisplayConfig:_config.display];

    [[PlaybackHub sharedHub] addObserver:self];
    [self handleTrackChanged];

    if (self.syncTimer == nil) {
        __weak __typeof__(self) weakSelf = self;
        self.syncTimer = [NSTimer scheduledTimerWithTimeInterval:kSyncTickInterval repeats:YES
                                                           block:^(NSTimer *timer) {
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) { [timer invalidate]; return; }
            [strongSelf tickSync];
        }];
    }
}

- (void)viewWillDisappear {
    [super viewWillDisappear];
    [[PlaybackHub sharedHub] removeObserver:self];
    [self.syncTimer invalidate];
    self.syncTimer = nil;
    [_searchPopover close];
}

- (void)dealloc {
    [self.syncTimer invalidate];
    [[PlaybackHub sharedHub] removeObserver:self];
}

#pragma mark - PlaybackHubObserving

- (void)playbackHubDidChange {
    [self handleTrackChanged];
}

#pragma mark - 曲目切换：动态源管线

- (void)handleTrackChanged {
    self.trackRequestToken += 1;
    const NSInteger requestToken = self.trackRequestToken;

    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) {
        _currentLyricData = openlyrics::LyricData{};
        _currentExtraOffsetMs = 0;
        _currentSourceLabel.clear();
        [self.lyricView setLyricData:_currentLyricData];
        self.statusLabel.stringValue = kPlaceholderText;
        self.offsetContainer.hidden = YES;
        return;
    }

    openlyrics::TrackMeta meta = [hub currentTrack];
    NSString *title = meta.title.empty() ? @"(未知曲目)"
        : [NSString stringWithUTF8String:meta.title.c_str()];
    self.statusLabel.stringValue = [NSString stringWithFormat:@"%@ · 检索歌词中…", title];

    // 拷贝 config，后台闭包只读
    openlyrics::AppConfig config = _config;
    int maxFail = config.maxConsecutiveFailures;

    // __block 拷贝避免后台线程直接读写 ivar 形成竞态
    __block int lrclibFails = _lrclibFailures;
    __block int neteaseFails = _neteaseFailures;
    __block int qqmusicFails = _qqmusicFailures;

    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        openlyrics::TagIOAdapter tagAdapter;
        openlyrics::FileSystemAdapter fsAdapter;
        openlyrics::TagSource tagSource(tagAdapter);
        openlyrics::LocalFileSource localSource(fsAdapter);
        openlyrics::SearchPipeline pipeline({&tagSource, &localSource});

        openlyrics::LyricData resolved;
        bool found = pipeline.resolve(meta, resolved);
        std::string sourceLabel = "none";
        if (found) {
            openlyrics::LyricData tagProbe;
            sourceLabel = tagSource.fetch(meta, tagProbe) ? "tag" : "local";
        }

        bool onlineSaved = false;
        auto trySource = [&](const char* label, const char* statusText, auto& provider,
                             bool& found, openlyrics::LyricData& resolved,
                             bool& saved, int& failures) -> bool {
            if (failures >= maxFail) return false;
            dispatch_async(dispatch_get_main_queue(), ^{
                __typeof__(self) strongSelf = weakSelf;
                if (strongSelf == nil) return;
                if (strongSelf.trackRequestToken != requestToken) return;
                strongSelf.statusLabel.stringValue =
                    [NSString stringWithFormat:@"%@ · %s", title, statusText];
            });
            openlyrics::LyricData data;
            if (provider.fetch(meta, data)) {
                found = true; resolved = data; sourceLabel = label; failures = 0;
                openlyrics::LyricStore store(fsAdapter);
                saved = store.save(meta, data);
                return true;
            }
            ++failures;
            return false;
        };

        // 按 config.sources 顺序遍历在线源
        for (const auto& src : config.sources) {
            if (found) break;
            if (!src.enabled) continue;
            if (src.key == "tag" || src.key == "local") continue;  // 已由 SearchPipeline 覆盖

            if (src.key == "lrclib" && lrclibFails < maxFail) {
                openlyrics::HttpAdapter http;
                openlyrics::LrcLibProvider lrcLib(http);
                trySource("online", "在线获取中…", lrcLib, found, resolved, onlineSaved,
                          lrclibFails);
            } else if (src.key == "netease" && neteaseFails < maxFail) {
                openlyrics::HttpAdapter http;
                openlyrics::CryptoAdapter crypto;
                openlyrics::NetEaseProvider netease(http, crypto);
                trySource("netease", "正在搜索网易云…", netease, found, resolved, onlineSaved,
                          neteaseFails);
            } else if (src.key == "qqmusic" && qqmusicFails < maxFail) {
                openlyrics::HttpAdapter http;
                openlyrics::CryptoAdapter crypto;
                openlyrics::QQMusicProvider qqmusic(http, crypto);
                trySource("qqmusic", "正在搜索QQ音乐…", qqmusic, found, resolved, onlineSaved,
                          qqmusicFails);
            }
        }

        if (!found) resolved = openlyrics::LyricData{};

        FB2K_console_print("foo_openlyrics: native path=", meta.path.c_str(),
                            found ? "  lyric=matched source=" : "  lyric=not-found source=",
                            sourceLabel.c_str(),
                            (sourceLabel == "online" || sourceLabel == "netease" || sourceLabel == "qqmusic")
                                ? (onlineSaved ? "  saved=yes" : "  saved=no") : "");

        dispatch_async(dispatch_get_main_queue(), ^{
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) return;
            if (strongSelf.trackRequestToken != requestToken) return;

            strongSelf->_lrclibFailures = lrclibFails;
            strongSelf->_neteaseFailures = neteaseFails;
            strongSelf->_qqmusicFailures = qqmusicFails;
            strongSelf->_currentLyricData = resolved;
            strongSelf->_currentExtraOffsetMs = config.defaultOffsetMs;
            strongSelf->_currentSourceLabel = sourceLabel;
            [strongSelf.lyricView setLyricData:resolved];
            strongSelf.statusLabel.stringValue = found ? title
                : [NSString stringWithFormat:@"%@ · 未找到歌词", title];
            strongSelf.offsetContainer.hidden = resolved.lines.empty();
            [strongSelf updateOffsetUI];
        });
    });
}

#pragma mark - 播放位置 tick

- (void)tickSync {
    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) return;

    auto pc = playback_control::get();
    int64_t posMs = pc.is_empty() ? [hub positionMs]
        : static_cast<int64_t>(pc->playback_get_position() * 1000.0 + 0.5);
    if (posMs < 0) posMs = 0;

    openlyrics::SyncResult result = openlyrics::SyncEngine::locate(
        _currentLyricData, posMs, _currentExtraOffsetMs);
    [self.lyricView setSyncResult:result];
}

@end
