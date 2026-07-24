# 桌面歌词逐句手动打轴 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 桌面歌词窗口的滚轮/上下方向键改为逐句手动打轴（tap-to-sync），右键锁定当前句、下键推进并把该句时标设为当前播放位置、上键回退撤销，结果立即写回 lrc，主面板高亮同步。

**Architecture:** 相邻时标行查找抽为 `SyncEngine::adjacentTimedLine` 纯函数（gtest）；`PlaybackHub` 新增手动高亮广播；`DesktopLyricsController` 撤销此前滚轮/方向键调 offset 逻辑并新增打轴状态机（发起方，写盘 + 广播）；`LyricPanelController` 用手动高亮索引覆盖 locate（接收方）。

**Tech Stack:** Objective-C++ / AppKit / foobar2000 SDK / C++17 核心 / GoogleTest。

## Global Constraints

- 方向：下键/下滚（`deltaY<0`）→ dir=+1 前进（推进并写时标）；上键/上滚（`deltaY>0`）→ dir=-1 回退（撤销）。
- 打轴仅在 `_tapActive` 为真时响应上下键/滚轮；右键（→）进入/重设锚点。音乐正常播放，绝不 seek。
- 无时标行（`timeMs<0`）在推进时跳过；到末个时标行下键停住；锚点初始可为 -1。
- 每次推进：改 `lines[target].timeMs` → 重建 `sourceText = LrcSerializer::serialize(data)` 且 `synced=true` → `LyricStore::forceSave([[PlaybackHub sharedHub] currentTrack], data)` 写盘 → `broadcastLyricChangedFromSelf` → `notifyManualHighlightLine:_tapAnchorLine`。
- 退出打轴（窗口失去 key 焦点 / 切歌）：`_tapActive=NO`、清空撤销栈、`notifyManualHighlightLine:-1`。
- 保留不动：右键菜单 ±500/±1000/重置偏移、Option+拖拽微调（`onOffsetDelta`/`onOffsetCommit`/`showOffsetHudWithDelta`/`_scrubDeltaMs`/`fadeHud`）、窗口移动/缩放/边缘 resize、键盘焦点基础设施（`DeskLyricsPanel`/`acceptsFirstResponder`/`mouseDown:` 的 `makeFirstResponder`）。

---

### Task 1: SyncEngine 相邻时标行纯函数

**Files:**
- Modify: `extensions/foo_openlyrics_mac/core/sync/SyncEngine.h`
- Modify: `extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp`
- Test: `tests/test_sync_engine.cpp`

**Interfaces:**
- Consumes: 既有 `LyricData`。
- Produces: `static int SyncEngine::adjacentTimedLine(const LyricData& data, int fromLine, int dir);`

- [ ] **Step 1: 写失败测试**

在 `tests/test_sync_engine.cpp` 末尾追加（`makeData()` 三行时标 a=1000/b=3000/c=5000 已在文件顶部）：

```cpp
TEST(SyncEngine, AdjacentForward) {
    EXPECT_EQ(SyncEngine::adjacentTimedLine(makeData(), 0, 1), 1);
}

TEST(SyncEngine, AdjacentBackward) {
    EXPECT_EQ(SyncEngine::adjacentTimedLine(makeData(), 1, -1), 0);
}

TEST(SyncEngine, AdjacentForwardStopsAtLast) {
    EXPECT_EQ(SyncEngine::adjacentTimedLine(makeData(), 2, 1), 2);
}

TEST(SyncEngine, AdjacentBackwardStopsAtFirst) {
    EXPECT_EQ(SyncEngine::adjacentTimedLine(makeData(), 0, -1), 0);
}

TEST(SyncEngine, AdjacentFromBeforeFirstForward) {
    EXPECT_EQ(SyncEngine::adjacentTimedLine(makeData(), -1, 1), 0);
}

TEST(SyncEngine, AdjacentFromBeforeFirstBackward) {
    EXPECT_EQ(SyncEngine::adjacentTimedLine(makeData(), -1, -1), -1);
}

TEST(SyncEngine, AdjacentSkipsUntimedForward) {
    LyricData d;
    d.synced = true;
    d.lines.push_back({1000, "a", {}});
    d.lines.push_back({-1, "note", {}});
    d.lines.push_back({3000, "b", {}});
    EXPECT_EQ(SyncEngine::adjacentTimedLine(d, 0, 1), 2);   // 跳过 note
}

TEST(SyncEngine, AdjacentSkipsUntimedBackward) {
    LyricData d;
    d.synced = true;
    d.lines.push_back({1000, "a", {}});
    d.lines.push_back({-1, "note", {}});
    d.lines.push_back({3000, "b", {}});
    EXPECT_EQ(SyncEngine::adjacentTimedLine(d, 2, -1), 0);  // 跳过 note
}

TEST(SyncEngine, AdjacentNoTimedLines) {
    LyricData d;
    d.synced = true;
    d.lines.push_back({-1, "only note", {}});
    EXPECT_EQ(SyncEngine::adjacentTimedLine(d, -1, 1), -1);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd ~/foo_openlyrics_mac/build && cmake --build . --target core_tests 2>&1 | tail -5`
Expected: 编译失败，`no member named 'adjacentTimedLine' in 'openlyrics::SyncEngine'`。

- [ ] **Step 3: 头文件声明**

编辑 `extensions/foo_openlyrics_mac/core/sync/SyncEngine.h`，在 `locate(...)` 声明之后、类结束 `};` 之前加：

```cpp

    // 从 fromLine（data.lines 下标，-1=尚未到首句）沿 dir(+1 前进/-1 后退) 找相邻的有时标行下标。
    // 跳过 timeMs<0 的行。端点无更多时标行时返回 fromLine（停住）；fromLine==-1 且无可达时标行返回 -1。
    static int adjacentTimedLine(const LyricData& data, int fromLine, int dir);
```

- [ ] **Step 4: 实现定义**

编辑 `extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp`，在 `locate(...)` 之后、`}  // namespace openlyrics` 之前加：

```cpp
int SyncEngine::adjacentTimedLine(const LyricData& data, int fromLine, int dir) {
    const int n = (int)data.lines.size();
    if (dir > 0) {
        for (int i = fromLine + 1; i < n; ++i)
            if (data.lines[i].timeMs >= 0) return i;
    } else {
        for (int i = fromLine - 1; i >= 0; --i)
            if (data.lines[i].timeMs >= 0) return i;
    }
    return fromLine >= 0 ? fromLine : -1;   // 端点停住；fromLine==-1 无可达则 -1
}
```

- [ ] **Step 5: 运行测试确认通过**

Run: `cd ~/foo_openlyrics_mac/build && cmake --build . --target core_tests 2>&1 | tail -3 && ./core_tests --gtest_filter='SyncEngine.*' 2>&1 | tail -3`
Expected: 全部 PASS（含新增 9 个用例）。

- [ ] **Step 6: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/core/sync/SyncEngine.h extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp tests/test_sync_engine.cpp
git commit -m "新增相邻时标行查找纯函数与测试"
```

---

### Task 2: PlaybackHub 手动高亮广播

**Files:**
- Modify: `extensions/foo_openlyrics_mac/platform/PlaybackBridge.h`
- Modify: `extensions/foo_openlyrics_mac/platform/PlaybackBridge.mm`

**Interfaces:**
- Consumes: 既有观察者 `NSHashTable`、`respondsToSelector:` 风格。
- Produces: 协议可选方法 `- (void)playbackHubManualHighlightDidChange:(NSInteger)lineIndex;`；`PlaybackHub` 方法 `- (void)notifyManualHighlightLine:(NSInteger)lineIndex;`。

- [ ] **Step 1: 协议加可选方法**

编辑 `PlaybackBridge.h`，在协议 `PlaybackHubObserving` 的 `- (void)playbackHubLyricDidChange;`（约 30 行）之后、`@end` 之前加：

```objc
// 桌面歌词打轴期间手动高亮某行；lineIndex>=0 高亮该行，-1 表示清除手动高亮、恢复自动跟随。
- (void)playbackHubManualHighlightDidChange:(NSInteger)lineIndex;
```

- [ ] **Step 2: 接口声明广播方法**

编辑 `PlaybackBridge.h`，在 `- (void)notifyLyricChanged;`（约 52 行）之后、`@end` 之前加：

```objc
// 广播打轴手动高亮行索引给所有观察者（-1 表示恢复自动跟随）。
- (void)notifyManualHighlightLine:(NSInteger)lineIndex;
```

- [ ] **Step 3: 实现广播方法**

编辑 `PlaybackBridge.mm`，在 `- (void)notifyLyricChanged { ... }`（约 192-201 行）方法结束的 `}` 之后加（遍历写法与 `notifyLyricChanged` 一致：锁内取快照、锁外遍历、`respondsToSelector:` 判断可选方法）：

```objc
- (void)notifyManualHighlightLine:(NSInteger)lineIndex {
    [_observersLock lock];
    NSArray<id<PlaybackHubObserving>> *snapshot = [_observers allObjects];
    [_observersLock unlock];

    for (id<PlaybackHubObserving> observer in snapshot) {
        if ([observer respondsToSelector:@selector(playbackHubManualHighlightDidChange:)]) {
            [observer playbackHubManualHighlightDidChange:lineIndex];
        }
    }
}
```

- [ ] **Step 4: 构建确认通过**

Run: `cd ~/foo_openlyrics_mac/build && /opt/homebrew/bin/cmake --build . --target foo_openlyrics 2>&1 | tail -4`
Expected: 链接成功，仅既有 pfc NDEBUG 警告。

- [ ] **Step 5: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/platform/PlaybackBridge.h extensions/foo_openlyrics_mac/platform/PlaybackBridge.mm
git commit -m "PlaybackHub 新增手动高亮广播"
```

---

### Task 3: DesktopLyricsController 打轴状态机

**Files:**
- Modify: `extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm`

**Interfaces:**
- Consumes: `SyncEngine::adjacentTimedLine`（Task 1）、`PlaybackHub notifyManualHighlightLine:`（Task 2）、既有 `LrcSerializer::serialize`、`LyricStore::forceSave`、`broadcastLyricChangedFromSelf`、`currentPositionMs`、`_lyricView setSyncResult:`。
- Produces: `DeskLyricsContent` 回调 `onTapAnchor`、`onTapStep`；控制器方法 `tapAnchorAtCurrent`、`tapStep:`、`freezeHighlightTo:`、`persistTapAndBroadcast`、`exitTapSync`。

- [ ] **Step 1: 删除滚轮/方向键调 offset 的常量**

删除文件顶部（约 39-41 行）三行：

```objc
static const int64_t kScrollMsPerWheelTick = 100;    // 滚轮每刻度偏移量（毫秒）
static const NSTimeInterval kScrollCommitDelay = 0.8; // 滚轮停止后多久自动提交偏移
static const int64_t kArrowOffsetStepMs = 100;       // 方向键每次调整的偏移量（毫秒）
```

替换为：

```objc
static const CGFloat kTapPixelsPerStep = 40.0;       // 触控板精确滚动每累积此像素打轴一步
```

- [ ] **Step 2: 替换滚轮回调属性、删除 scrollAccumMs 属性**

在 `DeskLyricsContent` 的 `@interface`，将 `onOffsetCommit` 声明（约 121 行）之后的 `onMoveTo` 之前，加打轴回调：

```objc
@property(nonatomic, copy) void (^onTapAnchor)(void);        // 右键锁定当前句为锚点
@property(nonatomic, copy) void (^onTapStep)(int dir);       // 上下键/滚轮打轴一步：+1 前进/-1 回退
```

并删除 `- (void)showMessage:(NSString *)text;` 之后的 scrollAccumMs 属性（约 145-146 行，含注释）：

```objc

// 当前滚轮累积的偏移量（毫秒），供外部在提交后清零。
@property(nonatomic, assign) int64_t scrollAccumMs;
```

- [ ] **Step 3: 替换滚轮 ivar**

将 ivar 块（约 174-175 行）：

```objc
    int64_t _scrollAccumMs;         // 滚轮累积偏移量
    NSTimer *_scrollCommitTimer;    // 滚轮停止后延迟提交
```

替换为：

```objc
    CGFloat _tapAccumPx;            // 触控板精确滚动的像素累积，达阈值打轴一步
```

- [ ] **Step 4: 重写 scrollWheel:、删除 applyOffsetDelta:、改 keyDown:**

将 `scrollWheel:`、`applyOffsetDelta:`、`scrollAccumMs` 存取器、`keyDown:` 整段（约 502-566 行，从 `- (void)scrollWheel:(NSEvent *)event {` 到 `keyDown:` 结束的 `}`）替换为：

```objc
- (void)scrollWheel:(NSEvent *)event {
    // 拖拽/缩放/边缘调整进行中时不响应，交回系统
    if (_isDragging || _isScrubbing || _isResizing || _isCmdResizing) {
        [super scrollWheel:event];
        return;
    }

    CGFloat dy = event.deltaY;
    if (fabs(dy) < 0.01) return;

    int steps = 0;
    if (event.hasPreciseScrollingDeltas) {
        // 触控板：按像素累积，达阈值一步；下滑(dy<0)->前进(dir=+1)
        _tapAccumPx += dy;
        while (_tapAccumPx <= -kTapPixelsPerStep) { _tapAccumPx += kTapPixelsPerStep; steps += 1; }
        while (_tapAccumPx >= kTapPixelsPerStep)  { _tapAccumPx -= kTapPixelsPerStep; steps -= 1; }
    } else {
        // 传统滚轮：每格一步，下滚(dy<0)->前进
        steps = (dy < 0) ? 1 : -1;
    }

    if (steps != 0 && self.onTapStep) self.onTapStep(steps > 0 ? 1 : -1);
}

- (BOOL)acceptsFirstResponder { return YES; }

- (void)keyDown:(NSEvent *)event {
    NSString *chars = event.charactersIgnoringModifiers;
    if (chars.length == 1) {
        unichar c = [chars characterAtIndex:0];
        if (c == NSRightArrowFunctionKey) {
            if (self.onTapAnchor) self.onTapAnchor();   // 右键：锁定当前句为锚点
            return;
        }
        if (c == NSDownArrowFunctionKey) {
            if (self.onTapStep) self.onTapStep(1);      // 下键：前进并写时标
            return;
        }
        if (c == NSUpArrowFunctionKey) {
            if (self.onTapStep) self.onTapStep(-1);     // 上键：回退撤销
            return;
        }
    }
    [super keyDown:event];
}
```

> `acceptsFirstResponder` 若原本紧邻 keyDown 已一并含在替换区间；替换后确保它仍只出现一次。

- [ ] **Step 5: 打轴状态 ivar**

在控制器 `@interface DesktopLyricsController () <NSWindowDelegate>` 的 ivar 区（`_currentLyricData`/`_currentExtraOffsetMs` 所在处，约 745 行之后的花括号块内）加：

```objc
    BOOL _tapActive;                             // 是否处于打轴模式
    int _tapAnchorLine;                          // 当前手动高亮行（data.lines 下标，-1 合法）
    std::vector<std::pair<int,int64_t>> _tapUndo; // 每次前进前保存 (行下标, 原 timeMs)，供上键撤销
```

Run 确认头部已含 `#include <vector>` 与 `<utility>`（LyricData.h 已带 `<vector>`/`<utility>`，经 include 传递）：
`grep -n "#include <vector>\|LyricData.h" extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm`

- [ ] **Step 6: 控制器打轴方法**

在 `- (int64_t)currentPositionMs` 方法（约 1397 行）之后加：

```objc
#pragma mark - 逐句手动打轴

// 冻结桌面高亮到指定行（progress=0），避免下一次 tickSync 用播放位置覆盖。
- (void)freezeHighlightTo:(int)line {
    openlyrics::SyncResult r;
    r.lineIndex = line;
    r.progress = 0.0;
    [_lyricView setSyncResult:r];
}

// 右键：锁定当前高亮句为锚点，进入打轴模式。
- (void)tapAnchorAtCurrent {
    if (_currentLyricData.lines.empty()) return;
    _tapActive = YES;
    openlyrics::SyncResult cur = openlyrics::SyncEngine::locate(
        _currentLyricData, [self currentPositionMs], _currentExtraOffsetMs);
    _tapAnchorLine = cur.lineIndex;
    _tapUndo.clear();
    [self freezeHighlightTo:_tapAnchorLine];
    [[PlaybackHub sharedHub] notifyManualHighlightLine:_tapAnchorLine];
}

// 上下键/滚轮一步：dir>0 前进并写时标，dir<0 回退撤销。
- (void)tapStep:(int)dir {
    if (!_tapActive || _currentLyricData.lines.empty()) return;

    if (dir > 0) {
        int next = openlyrics::SyncEngine::adjacentTimedLine(_currentLyricData, _tapAnchorLine, 1);
        if (next == _tapAnchorLine || next < 0) return;   // 末句停住 / 无时标行
        _tapUndo.push_back({next, _currentLyricData.lines[next].timeMs});
        _currentLyricData.lines[next].timeMs = [self currentPositionMs];
        _tapAnchorLine = next;
    } else {
        // 撤销当前锚点行本次打轴（若栈顶正是它）
        if (!_tapUndo.empty() && _tapUndo.back().first == _tapAnchorLine) {
            _currentLyricData.lines[_tapAnchorLine].timeMs = _tapUndo.back().second;
            _tapUndo.pop_back();
        }
        int prev = openlyrics::SyncEngine::adjacentTimedLine(_currentLyricData, _tapAnchorLine, -1);
        if (prev == _tapAnchorLine) {                     // 首句停住：仅可能已撤销，无行移动
            [self persistTapAndBroadcast];
            return;
        }
        _tapAnchorLine = prev;
    }
    [self persistTapAndBroadcast];
}

// 重建 sourceText、写盘、刷新桌面高亮、广播主面板（数据 + 手动高亮）。
- (void)persistTapAndBroadcast {
    _currentLyricData.sourceText = openlyrics::LrcSerializer::serialize(_currentLyricData);
    _currentLyricData.synced = true;

    openlyrics::TrackMeta meta = [[PlaybackHub sharedHub] currentTrack];
    openlyrics::FileSystemAdapter fsAdapter;
    openlyrics::LyricStore store(fsAdapter);
    if (store.forceSave(meta, _currentLyricData)) {
        _currentLyricPath = openlyrics::LocalFileSource::stripExtension(meta.path) + ".lrc";
        _contentView.canDeleteLyric = YES;
    }

    [_lyricView setLyricData:_currentLyricData];
    [self freezeHighlightTo:_tapAnchorLine];
    [self broadcastLyricChangedFromSelf];                       // 主面板重载新时标
    [[PlaybackHub sharedHub] notifyManualHighlightLine:_tapAnchorLine];  // 主面板高亮同一句
}

// 退出打轴，恢复自动跟随。
- (void)exitTapSync {
    if (!_tapActive) return;
    _tapActive = NO;
    _tapUndo.clear();
    [[PlaybackHub sharedHub] notifyManualHighlightLine:-1];
}
```

- [ ] **Step 7: tickSync 打轴时冻结高亮**

将 `- (void)tickSync {`（约 1385 行）方法体开头改为在原逻辑前插入冻结分支：

```objc
- (void)tickSync {
    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) return;

    if (_tapActive) {                       // 打轴中：冻结在手动锚点，不随播放定位
        [self freezeHighlightTo:_tapAnchorLine];
        return;
    }

    int64_t posMs = [self currentPositionMs];
```

（其余原有 `if (posMs < 0)...locate...setSyncResult` 保持不变。）

- [ ] **Step 8: 接线回调**

在控制器 setup 块 `_contentView.onOffsetCommit = ^...`（约 925 行）赋值之后加：

```objc
    _contentView.onTapAnchor = ^{
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        [strongSelf tapAnchorAtCurrent];
    };
    _contentView.onTapStep = ^(int dir) {
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        [strongSelf tapStep:dir];
    };
```

- [ ] **Step 9: 退出打轴挂钩（失焦 + 切歌）**

在 `#pragma mark - NSWindowDelegate`（约 1459 行）区加窗口失焦退出：

```objc
- (void)windowDidResignKey:(NSNotification *)notification {
    [self exitTapSync];
}
```

并在 `- (void)playbackHubDidChange {`（约 1148 行）方法体首行加切歌退出：

```objc
- (void)playbackHubDidChange {
    [self exitTapSync];
    [self handleTrackChanged];
}
```

- [ ] **Step 10: 全局检查残留引用**

Run: `cd ~/foo_openlyrics_mac && grep -rn "applyOffsetDelta\|_scrollAccumMs\|_scrollCommitTimer\|scrollAccumMs\|kScrollMsPerWheelTick\|kScrollCommitDelay\|kArrowOffsetStepMs" extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm`
Expected: 无输出。

- [ ] **Step 11: 构建并安装**

Run:
```bash
cd ~/foo_openlyrics_mac/build && /opt/homebrew/bin/cmake --build . --target foo_openlyrics 2>&1 | tail -4 && cd ~/foo_openlyrics_mac && bash Scripts/install-component.sh 2>&1 | tail -2
```
Expected: 链接成功、安装完成，仅既有 pfc NDEBUG 警告。

- [ ] **Step 12: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm
git commit -m "桌面歌词滚轮/方向键改为逐句手动打轴"
```

---

### Task 4: LyricPanelController 手动高亮覆盖与手动验证

**Files:**
- Modify: `extensions/foo_openlyrics_mac/ui/LyricPanelController.mm`

**Interfaces:**
- Consumes: Task 2 的 `playbackHubManualHighlightDidChange:`；既有 `self.lyricView setSyncResult:`、`tickSync`。
- Produces: 无对外符号（仅补齐接收方行为）。

- [ ] **Step 1: 手动高亮 ivar**

在 `@implementation LyricPanelController {`（约 70 行）的 ivar 块，`int64_t _currentExtraOffsetMs;`（约 72 行）之后加：

```objc
    NSInteger _manualHighlightLine;   // 打轴手动高亮行；-1=自动跟随
```

并在初始化处（`_currentExtraOffsetMs = 0;` 约 301 行）之后同一方法内加初值：

```objc
    _manualHighlightLine = -1;
```

- [ ] **Step 2: 实现协议方法**

在 `- (void)playbackHubLyricDidChange {`（约 672 行）方法之前或之后加：

```objc
- (void)playbackHubManualHighlightDidChange:(NSInteger)lineIndex {
    _manualHighlightLine = lineIndex;
    if (lineIndex >= 0) {
        openlyrics::SyncResult r;
        r.lineIndex = (int)lineIndex;
        r.progress = 0.0;
        [self.lyricView setSyncResult:r];
    } else {
        [self tickSync];   // 恢复自动：立即按当前播放位置刷新一次
    }
}
```

- [ ] **Step 3: tickSync 手动高亮覆盖**

将 `- (void)tickSync {`（约 833 行）方法体在 `if (![hub hasTrack]) return;` 之后插入：

```objc
    if (_manualHighlightLine >= 0) {           // 打轴中：显示桌面广播的手动高亮行
        openlyrics::SyncResult r;
        r.lineIndex = (int)_manualHighlightLine;
        r.progress = 0.0;
        [self.lyricView setSyncResult:r];
        return;
    }
```

- [ ] **Step 4: 构建并安装**

Run:
```bash
cd ~/foo_openlyrics_mac/build && /opt/homebrew/bin/cmake --build . --target foo_openlyrics 2>&1 | tail -4 && cd ~/foo_openlyrics_mac && bash Scripts/install-component.sh 2>&1 | tail -2
```
Expected: 链接成功、安装完成。

- [ ] **Step 5: 全套测试回归**

Run: `cd ~/foo_openlyrics_mac/build && ./core_tests 2>&1 | tail -2`
Expected: `[  PASSED  ] 233 tests.`（224 基线 + Task 1 新增 9）。

- [ ] **Step 6: 手动验证（foobar）**

重启 foobar2000，播放一首歌词与人声不同步的曲目，单击桌面歌词窗口取得焦点：
- 按右键（→）：桌面与主面板均把高亮冻结在当前句。
- 音乐继续放，唱到下一句起点时按下键：高亮推进到下一句，该句时标被设为按键时刻；主面板高亮同步到同一句。滚轮下滚等效下键。
- 上键/上滚：回退一句并撤销刚打的时标。
- 反复打几句后切歌再切回：新时标已写入 lrc（高亮时机与打轴一致）。
- 点击歌词窗口外（失去焦点）或切歌：两面板恢复随播放自动跟随。
- 到最后一句后再按下键：高亮停住不动。
- 无时标署名行被跳过。
- 右键菜单 ±500/±1000/重置偏移、Option+拖拽、窗口拖动/缩放不受影响。

- [ ] **Step 7: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/ui/LyricPanelController.mm
git commit -m "主面板支持打轴手动高亮同步"
```

---

## 自查

- **Spec 覆盖**：相邻时标行→Task 1；手动高亮广播→Task 2；打轴状态机（进入/推进/撤销/写盘/冻结/退出）→Task 3 Step 5-9；主面板高亮覆盖→Task 4；无时标跳过/末句停/锚点-1→Task 1 纯函数 + Task 3 `tapStep:` 守卫；写盘重建 sourceText→Task 3 `persistTapAndBroadcast`；退出恢复→Task 3 `exitTapSync` + Task 4 tickSync 覆盖清除。全部有对应任务。
- **占位扫描**：无 TBD/TODO；各步骤均给完整代码与锚点行号（Task 2 Step 3 已按 `notifyLyricChanged` 实际的锁内快照+锁外遍历写法落地）。
- **类型一致**：`adjacentTimedLine(const LyricData&, int, int)→int`（Task 1 定义、Task 3 调用一致）；`notifyManualHighlightLine:(NSInteger)`／`playbackHubManualHighlightDidChange:(NSInteger)`（Task 2 定义、Task 3 发送、Task 4 接收一致）；`onTapAnchor`(void)、`onTapStep`(int)（Task 3 属性定义与接线、keyDown/scrollWheel 调用一致）；`_tapActive`/`_tapAnchorLine`/`_tapUndo`（Task 3 内部一致）；`_manualHighlightLine`(NSInteger,-1 哨兵)（Task 4 内部一致）。
