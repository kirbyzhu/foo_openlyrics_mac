// LyricPanelController.mm
// foo_openlyrics_mac —— Plan 2-4 歌词面板控制器；
// Plan 5 Task 2-4：新增手动搜索（NSSearchField + NSPopover + NSTableView）、
// offset 微调（NSStepper + 标签 + extraOffsetMs）、offset 持久化（forceSave 覆写 .lrc）。
//
// 线程切分：
//   - PlaybackHub 回调在主线程触发。
//   - 耗时检索（管线 + 在线搜索 + fetchById）在后台并发队列执行。
//   - 结果 dispatch 回主线程后才碰 UI（AppKit 对象）。
//   - _trackRequestToken 自增作废过期结果。
#import "LyricPanelController.h"
#import "stdafx.h"

#import "LyricView.h"
#import "TagIOAdapter.h"
#import "FileSystemAdapter.h"
#import "HttpAdapter.h"
#import "CryptoAdapter.h"

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

static NSString *const kPlaceholderText = @"未在播放";
static const int kMaxConsecutiveFailures = 5;
static const NSTimeInterval kSyncTickInterval = 0.06;
static const double kOffsetStep = 0.1;      // 每步 100ms
static const double kOffsetMin = -30.0;
static const double kOffsetMax = 30.0;

@interface LyricPanelController () <NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic, strong) NSSearchField *searchField;
@property(nonatomic, strong) NSTextField *statusLabel;
@property(nonatomic, strong) LyricView *lyricView;
@property(nonatomic, strong) NSTimer *syncTimer;
@property(nonatomic, assign) NSInteger trackRequestToken;

// offset 微调控件
@property(nonatomic, strong) NSView *offsetContainer;
@property(nonatomic, strong) NSStepper *offsetStepper;
@property(nonatomic, strong) NSTextField *offsetLabel;
@property(nonatomic, strong) NSButton *applyOffsetBtn;

// 搜索弹窗
@property(nonatomic, strong) NSPopover *searchPopover;
@property(nonatomic, strong) NSTableView *searchTableView;
@property(nonatomic, copy) NSArray<NSDictionary *> *searchResults;
@end

@implementation LyricPanelController {
    openlyrics::LyricData _currentLyricData;
    int64_t _currentExtraOffsetMs;
    std::string _currentSourceLabel;  // "tag"/"local"/"online"/"netease"/"qqmusic"
    int _lrclibFailures;
    int _neteaseFailures;
    int _qqmusicFailures;
}

- (void)loadView {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 200)];

    // 搜索框
    NSSearchField *search = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    search.placeholderString = @"搜索 LrcLib…";
    search.translatesAutoresizingMaskIntoConstraints = NO;
    search.target = self;
    search.action = @selector(searchFieldAction:);
    [root addSubview:search];

    // 状态行 + offset 控件容器
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

    [offsetBox addSubview:stepper];
    [offsetBox addSubview:offsetLbl];
    [offsetBox addSubview:applyBtn];
    [statusRow addSubview:offsetBox];

    // LyricView
    LyricView *lyricView = [[LyricView alloc] initWithFrame:NSZeroRect];
    lyricView.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:lyricView];

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
        [applyBtn.trailingAnchor constraintEqualToAnchor:offsetBox.trailingAnchor],
        [applyBtn.centerYAnchor constraintEqualToAnchor:offsetBox.centerYAnchor],

        [lyricView.topAnchor constraintEqualToAnchor:statusRow.bottomAnchor constant:2],
        [lyricView.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [lyricView.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [lyricView.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],
    ]];

    // 搜索结果 popover
    _searchPopover = [[NSPopover alloc] init];
    _searchPopover.behavior = NSPopoverBehaviorTransient;
    _searchPopover.animates = YES;

    NSTableView *tv = [[NSTableView alloc] initWithFrame:NSZeroRect];
    NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:@"result"];
    col.title = @"";
    col.width = 260;
    [tv addTableColumn:col];
    tv.headerView = nil;
    tv.rowHeight = 36;
    tv.dataSource = self;
    tv.delegate = self;
    tv.target = self;
    tv.doubleAction = @selector(searchRowDoubleClicked:);
    _searchTableView = tv;

    NSScrollView *scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 280, 200)];
    scrollView.documentView = tv;
    scrollView.hasVerticalScroller = YES;
    NSViewController *popoverVC = [[NSViewController alloc] init];
    popoverVC.view = scrollView;
    _searchPopover.contentViewController = popoverVC;

    self.searchField = search;
    self.statusLabel = status;
    self.lyricView = lyricView;
    self.offsetContainer = offsetBox;
    self.offsetStepper = stepper;
    self.offsetLabel = offsetLbl;
    self.applyOffsetBtn = applyBtn;
    self.view = root;
}

#pragma mark - 搜索

- (void)searchFieldAction:(NSSearchField *)sender {
    NSString *query = [sender.stringValue stringByTrimmingCharactersInSet:
                       [NSCharacterSet whitespaceCharacterSet]];
    if (query.length == 0) return;

    // 关闭旧 popover 并显示搜索中状态
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
                @"id": @(r.id),
                @"trackName": [NSString stringWithUTF8String:r.trackName.c_str()],
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
                // 调整 popover 高度
                NSViewController *vc = strongSelf.searchPopover.contentViewController;
                CGFloat h = MIN(arr.count * strongSelf.searchTableView.rowHeight + 4, 200);
                vc.view.frame = NSMakeRect(0, 0, 280, h);
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
    NSDictionary *item = _searchResults[row];
    int lyricId = [item[@"id"] intValue];

    [_searchPopover close];
    self.searchField.stringValue = @"";

    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) return;
    openlyrics::TrackMeta meta = [hub currentTrack];
    NSString *title = meta.title.empty() ? @"(未知曲目)" :
        [NSString stringWithUTF8String:meta.title.c_str()];
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
                strongSelf.statusLabel.stringValue = [NSString stringWithFormat:@"%@ · 获取失败", title];
            });
            return;
        }

        openlyrics::FileSystemAdapter fsAdapter;
        openlyrics::LyricStore store(fsAdapter);
        bool saved = store.save(meta, data);

        dispatch_async(dispatch_get_main_queue(), ^{
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil || strongSelf.trackRequestToken != requestToken) return;
            strongSelf->_currentLyricData = data;
            strongSelf->_currentExtraOffsetMs = 0;
            strongSelf->_currentSourceLabel = "search";
            [strongSelf.lyricView setLyricData:data];
            strongSelf.statusLabel.stringValue = title;
            [strongSelf updateOffsetUI];
            strongSelf.offsetContainer.hidden = (data.lines.empty());
        });
    });
}

#pragma mark - NSTableViewDataSource

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    return (NSInteger)_searchResults.count;
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row {
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

#pragma mark - Offset 微调

- (void)offsetStepperAction:(NSStepper *)sender {
    _currentExtraOffsetMs = static_cast<int64_t>(sender.doubleValue * 1000.0);
    [self updateOffsetLabel];
}

- (void)applyOffsetAction:(NSButton *)sender {
    if (_currentExtraOffsetMs == 0) return;
    if (_currentLyricData.sourceText.empty()) return;

    // 把 extra offset 固化到 sourceText 的 [offset:] 标签中
    int64_t newTotalOffsetMs = _currentLyricData.offsetMs + _currentExtraOffsetMs;
    std::string newSourceText = _currentLyricData.sourceText;

    // 查找并替换已有 [offset:] 行，或插入新行
    size_t offsetPos = newSourceText.find("[offset:");
    if (offsetPos != std::string::npos) {
        size_t endPos = newSourceText.find(']', offsetPos);
        size_t lineEnd = newSourceText.find('\n', offsetPos);
        if (endPos != std::string::npos) {
            size_t eraseEnd = (lineEnd != std::string::npos) ? lineEnd : endPos + 1;
            newSourceText.erase(offsetPos, eraseEnd - offsetPos);
            if (lineEnd == std::string::npos && offsetPos > 0) {
                newSourceText.insert(offsetPos, "\n");
                offsetPos += 1;
            }
        }
    }

    std::string offsetLine = "[offset:" + std::to_string(newTotalOffsetMs) + "]";
    if (offsetPos == std::string::npos) {
        // 插入在最后一个 id 标签之后
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

    // 重新解析、更新内存状态
    _currentLyricData = openlyrics::LrcParser::parse(newSourceText);
    _currentLyricData.sourceText = newSourceText;
    _currentExtraOffsetMs = 0;

    // 覆写 .lrc
    PlaybackHub *hub = [PlaybackHub sharedHub];
    if ([hub hasTrack]) {
        openlyrics::TrackMeta meta = [hub currentTrack];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            openlyrics::FileSystemAdapter fsAdapter;
            openlyrics::LyricStore store(fsAdapter);
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

    [[PlaybackHub sharedHub] addObserver:self];
    [self handleTrackChanged];

    if (self.syncTimer == nil) {
        __weak __typeof__(self) weakSelf = self;
        self.syncTimer = [NSTimer scheduledTimerWithTimeInterval:kSyncTickInterval
                                                           repeats:YES
                                                             block:^(NSTimer *timer) {
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) {
                [timer invalidate];
                return;
            }
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

#pragma mark - 曲目切换

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

    NSString *title = meta.title.empty() ? @"(未知曲目)" : [NSString stringWithUTF8String:meta.title.c_str()];
    self.statusLabel.stringValue = [NSString stringWithFormat:@"%@ · 检索歌词中…", title];

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
        auto trySource = [&](const char* label, const char* statusText, auto& provider, bool& found,
                             openlyrics::LyricData& resolved, bool& saved, int& failures) -> bool {
            if (failures >= kMaxConsecutiveFailures) return false;
            dispatch_async(dispatch_get_main_queue(), ^{
                __typeof__(self) strongSelf = weakSelf;
                if (strongSelf == nil) return;
                if (strongSelf.trackRequestToken != requestToken) return;
                strongSelf.statusLabel.stringValue =
                    [NSString stringWithFormat:@"%@ · %s", title, statusText];
            });
            openlyrics::LyricData data;
            if (provider.fetch(meta, data)) {
                found = true;
                resolved = data;
                sourceLabel = label;
                failures = 0;
                openlyrics::LyricStore store(fsAdapter);
                saved = store.save(meta, data);
                return true;
            }
            ++failures;
            return false;
        };

        if (!found) {
            if (_lrclibFailures < kMaxConsecutiveFailures) {
                openlyrics::HttpAdapter httpAdapter;
                openlyrics::LrcLibProvider lrcLib(httpAdapter);
                trySource("online", "在线获取中…", lrcLib, found, resolved, onlineSaved, _lrclibFailures);
            }
        }
        if (!found) {
            if (_neteaseFailures < kMaxConsecutiveFailures) {
                openlyrics::HttpAdapter httpAdapter;
                openlyrics::CryptoAdapter crypto;
                openlyrics::NetEaseProvider netease(httpAdapter, crypto);
                trySource("netease", "正在搜索网易云…", netease, found, resolved, onlineSaved, _neteaseFailures);
            }
        }
        if (!found) {
            if (_qqmusicFailures < kMaxConsecutiveFailures) {
                openlyrics::HttpAdapter httpAdapter;
                openlyrics::CryptoAdapter crypto;
                openlyrics::QQMusicProvider qqmusic(httpAdapter, crypto);
                trySource("qqmusic", "正在搜索QQ音乐…", qqmusic, found, resolved, onlineSaved, _qqmusicFailures);
            }
        }

        if (!found) resolved = openlyrics::LyricData{};

        // 控制台日志
        FB2K_console_print("foo_openlyrics: native path=", meta.path.c_str(),
                            found ? "  lyric=matched source=" : "  lyric=not-found source=",
                            sourceLabel.c_str(),
                            (sourceLabel == "online" || sourceLabel == "netease" || sourceLabel == "qqmusic")
                                ? (onlineSaved ? "  saved=yes" : "  saved=no") : "");

        dispatch_async(dispatch_get_main_queue(), ^{
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) return;
            if (strongSelf.trackRequestToken != requestToken) return;

            strongSelf->_currentLyricData = resolved;
            strongSelf->_currentExtraOffsetMs = 0;
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
