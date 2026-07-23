# 桌面歌词滚轮/方向键逐行跳播 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 焦点/指针在桌面歌词窗口时，滚轮一格或上/下方向键切到上一句/下一句并 seek 音频到该句时间戳，替换原滚轮调偏移交互。

**Architecture:** 逐行→seek 目标的换算抽为 `SyncEngine` 纯静态函数（gtest 覆盖）；`DesktopLyricsController` 新增 `seekLyricLineBySteps:` 做真实 `playback_seek`；滚轮与方向键两条路径都经新回调 `onSeekLineSteps` 归于该方法。键盘经 `NSPanel` 子类开启 key 焦点实现。

**Tech Stack:** Objective-C++ / AppKit / foobar2000 SDK（`playback_control`）/ C++17 核心 / GoogleTest。

## Global Constraints

- 方向映射：滚轮上滚（`event.deltaY > 0`）与上方向键 → 上一句（更早，`steps < 0`）；下滚与下方向键 → 下一句（更晚，`steps > 0`）。
- 仅当 `_currentLyricData.synced` 且有时标行、且 `playback_can_seek()` 为真时生效；否则忽略输入。
- seek 换算：`seekMs = 目标行.timeMs - data.offsetMs - extraOffsetMs`，钳制 `>= 0`；对齐 `SyncEngine::locate` 的 `eff = positionMs + offsetMs + extraOffsetMs`。
- 目标行越界时钳制到 `[首个时标行, 末个时标行]`，不回绕。
- 不改动：Option+拖拽微调偏移（`onOffsetDelta`/`onOffsetCommit`/`showOffsetHudWithDelta`/`_scrubDeltaMs`）、右键菜单偏移项、`commitOffsetDelta`、`broadcastLyricChangedFromSelf`、窗口移动/缩放/边缘 resize。
- 主界面同步依赖真实 `playback_seek` 改全局播放位置，主面板以既有 `kSyncTickInterval=0.06s` tick 自动跟随，不加额外通知接线。

---

### Task 1: SyncEngine 逐行 seek 目标纯函数

**Files:**
- Modify: `extensions/foo_openlyrics_mac/core/sync/SyncEngine.h`
- Modify: `extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp`
- Test: `tests/test_sync_engine.cpp`

**Interfaces:**
- Consumes: 既有 `SyncEngine::locate(const LyricData&, int64_t positionMs, int64_t extraOffsetMs)`。
- Produces: `static int64_t SyncEngine::seekTargetForLineStep(const LyricData& data, int64_t positionMs, int steps, int64_t extraOffsetMs = 0);` 返回应 seek 到的毫秒（`>= 0`）；非 synced 或无时标行返回 `-1`。

- [ ] **Step 1: 写失败测试**

在 `tests/test_sync_engine.cpp` 末尾追加（`makeData()` 已在文件顶部，行时标 a=1000/b=3000/c=5000）：

```cpp
TEST(SyncEngine, StepForwardFromCurrentLine) {
    // 当前 pos=2000 落在 a(idx0)，+1 步 -> b(3000)，offset=0 -> seek 3000
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 2000, 1), 3000);
}

TEST(SyncEngine, StepBackwardFromCurrentLine) {
    // pos=4000 落在 b(idx1)，-1 步 -> a(1000)
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 4000, -1), 1000);
}

TEST(SyncEngine, StepClampsAtLastLine) {
    // pos=9000 落在 c(idx2，末行)，+1 步仍钳制到 c(5000)
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 9000, 1), 5000);
}

TEST(SyncEngine, StepClampsAtFirstLine) {
    // pos=2000 落在 a(idx0，首行)，-1 步钳制到 a(1000)
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 2000, -1), 1000);
}

TEST(SyncEngine, StepFromBeforeFirstLineForward) {
    // pos=500 在首句前(cur=-1)，+1 步 -> 首个时标行 a(1000)
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 500, 1), 1000);
}

TEST(SyncEngine, StepMultipleLines) {
    // pos=2000 落在 a(idx0)，+2 步 -> c(5000)
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 2000, 2), 5000);
}

TEST(SyncEngine, StepSkipsUntimedLines) {
    // 在 a 与 b 之间插入无时标行，步进应跳过它
    LyricData d;
    d.synced = true;
    d.lines.push_back({1000, "a", {}});
    d.lines.push_back({-1, "credit", {}});   // 无时标
    d.lines.push_back({3000, "b", {}});
    // pos=1500 落在 a，+1 步 -> b(3000)，而非无时标行
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(d, 1500, 1), 3000);
}

TEST(SyncEngine, StepAppliesOffsetInSeekTarget) {
    // data.offsetMs=-2000，目标 b.timeMs=3000 -> seek = 3000-(-2000)-0 = 5000
    LyricData d = makeData();
    d.offsetMs = -2000;
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(d, 2000, 1), 5000);
}

TEST(SyncEngine, StepSeekTargetClampsToZero) {
    // 目标 a.timeMs=1000，extraOffset=+5000 -> 1000-0-5000 = -4000 -> 钳制 0
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 4000, -1, 5000), 0);
}

TEST(SyncEngine, StepUnsyncedReturnsMinusOne) {
    LyricData d;
    d.synced = false;
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(d, 1000, 1), -1);
}

TEST(SyncEngine, StepNoTimedLinesReturnsMinusOne) {
    LyricData d;
    d.synced = true;
    d.lines.push_back({-1, "only credit", {}});
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(d, 0, 1), -1);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd ~/foo_openlyrics_mac/build && cmake --build . --target core_tests 2>&1 | tail -5`
Expected: 编译失败，`no member named 'seekTargetForLineStep' in 'openlyrics::SyncEngine'`。

- [ ] **Step 3: 在头文件声明**

编辑 `extensions/foo_openlyrics_mac/core/sync/SyncEngine.h`，在 `locate(...)` 声明之后、类结束 `};` 之前加：

```cpp
    // 从当前播放位置按 steps 步进到目标时标行，返回应 seek 到的播放位置（毫秒，>=0）。
    // steps>0 向后（更晚），<0 向前（更早）。目标钳制到 [首个时标行, 末个时标行]。
    // data 非 synced 或无时标行时返回 -1（调用方忽略）。
    static int64_t seekTargetForLineStep(const LyricData& data, int64_t positionMs,
                                         int steps, int64_t extraOffsetMs = 0);
```

- [ ] **Step 4: 在实现文件定义**

编辑 `extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp`，在文件顶部 `#include` 区加 `#include <vector>`，并在 `locate(...)` 函数之后、`}  // namespace openlyrics` 之前加：

```cpp
int64_t SyncEngine::seekTargetForLineStep(const LyricData& data, int64_t positionMs,
                                          int steps, int64_t extraOffsetMs) {
    if (!data.synced) return -1;

    // 收集有时标行的下标
    std::vector<int> timed;
    for (int i = 0; i < (int)data.lines.size(); ++i) {
        if (data.lines[i].timeMs >= 0) timed.push_back(i);
    }
    if (timed.empty()) return -1;

    // 当前行（data.lines 下标；-1 表示尚未到首句）
    int cur = locate(data, positionMs, extraOffsetMs).lineIndex;

    // 当前行在 timed 中的序号；cur==-1 记为 -1
    int ordinal = -1;
    for (int k = 0; k < (int)timed.size(); ++k) {
        if (timed[k] == cur) { ordinal = k; break; }
    }

    int targetOrd = ordinal + steps;
    if (targetOrd < 0) targetOrd = 0;
    if (targetOrd > (int)timed.size() - 1) targetOrd = (int)timed.size() - 1;

    int64_t targetTimeMs = data.lines[timed[targetOrd]].timeMs;
    int64_t seekMs = targetTimeMs - data.offsetMs - extraOffsetMs;
    if (seekMs < 0) seekMs = 0;
    return seekMs;
}
```

- [ ] **Step 5: 运行测试确认通过**

Run: `cd ~/foo_openlyrics_mac/build && cmake --build . --target core_tests 2>&1 | tail -3 && ./core_tests --gtest_filter='SyncEngine.*'`
Expected: 全部 PASS（含新增 11 个用例）。

- [ ] **Step 6: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/core/sync/SyncEngine.h extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp tests/test_sync_engine.cpp
git commit -m "新增逐行 seek 目标换算与测试"
```

---

### Task 2: 滚轮逐行跳播

**Files:**
- Modify: `extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm`

**Interfaces:**
- Consumes: `SyncEngine::seekTargetForLineStep(...)`（Task 1）；既有 `-currentPositionMs`、`-tickSync`、`playback_control::get()`。
- Produces: `DeskLyricsContent` 新属性 `@property(nonatomic, copy) void (^onSeekLineSteps)(int steps);`；`DesktopLyricsController` 新方法 `-(void)seekLyricLineBySteps:(int)steps;`。

- [ ] **Step 1: 加行步进像素阈值常量**

在文件顶部常量区（`kScrollMsPerWheelTick` 所在处，约 39 行）之后加：

```objc
static const CGFloat kScrollPixelsPerLine = 40.0;  // 触控板精确滚动每累积此像素步进一行
```

- [ ] **Step 2: 声明滚轮回调属性**

在 `DeskLyricsContent` 的 `@interface`/类扩展属性区（`onOffsetCommit` 声明约 111 行）之后加：

```objc
@property(nonatomic, copy) void (^onSeekLineSteps)(int steps);
```

- [ ] **Step 3: 替换滚轮偏移 ivar 为像素累加器**

在 `DeskLyricsContent` 的 ivar 块中，删除：

```objc
    int64_t _scrollAccumMs;         // 滚轮累积偏移量
    NSTimer *_scrollCommitTimer;    // 滚轮停止后延迟提交
```

替换为：

```objc
    CGFloat _scrollLineAccumPx;     // 触控板精确滚动的像素累积，达阈值步进一行
```

- [ ] **Step 4: 重写 scrollWheel:**

将 `- (void)scrollWheel:(NSEvent *)event { ... }` 整个方法体（约 491–529 行）替换为：

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
        // 触控板：按像素累积，达到阈值步进一行；上滑(dy>0)->上一句
        _scrollLineAccumPx += dy;
        while (_scrollLineAccumPx >= kScrollPixelsPerLine) {
            _scrollLineAccumPx -= kScrollPixelsPerLine;
            steps -= 1;
        }
        while (_scrollLineAccumPx <= -kScrollPixelsPerLine) {
            _scrollLineAccumPx += kScrollPixelsPerLine;
            steps += 1;
        }
    } else {
        // 传统滚轮：每格一行，上滚(dy>0)->上一句
        steps = (dy > 0) ? -1 : 1;
    }

    if (steps != 0 && self.onSeekLineSteps) self.onSeekLineSteps(steps);
}
```

- [ ] **Step 5: 删除滚轮偏移的 scrollAccumMs 访问器**

删除 `- (int64_t)scrollAccumMs { return _scrollAccumMs; }` 与 `- (void)setScrollAccumMs:(int64_t)v { _scrollAccumMs = v; }`（约 531–532 行），及其对应的 `@property ... scrollAccumMs`（约 136 行、含上一行注释「当前滚轮累积的偏移量…」）。

- [ ] **Step 6: 新增控制器 seek 方法**

在 `DesktopLyricsController` 的 `@implementation` 内、`commitOffsetDelta:` 之前加：

```objc
// 滚轮/方向键逐行跳播：seek 到目标时标行，主面板由其 0.06s tick 自动跟随。
- (void)seekLyricLineBySteps:(int)steps {
    if (steps == 0) return;
    auto pc = playback_control::get();
    if (pc.is_empty() || !pc->playback_can_seek()) return;

    int64_t seekMs = openlyrics::SyncEngine::seekTargetForLineStep(
        _currentLyricData, [self currentPositionMs], steps, _currentExtraOffsetMs);
    if (seekMs < 0) return;   // 非 synced / 无时标行

    pc->playback_seek(static_cast<double>(seekMs) / 1000.0);
    [self tickSync];          // 立即刷新桌面自身高亮
}
```

- [ ] **Step 7: 接线回调**

在控制器 setup 块中 `_contentView.onOffsetCommit = ^...` 赋值（约 888–892 行）之后加：

```objc
    _contentView.onSeekLineSteps = ^(int steps) {
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        [strongSelf seekLyricLineBySteps:steps];
    };
```

- [ ] **Step 8: 构建并安装**

Run:
```bash
cd ~/foo_openlyrics_mac/build && /opt/homebrew/bin/cmake --build . --target foo_openlyrics 2>&1 | tail -4 && cd ~/foo_openlyrics_mac && bash Scripts/install-component.sh 2>&1 | tail -2
```
Expected: 链接成功、ad-hoc 签名、安装完成，无编译错误（仅既有 pfc NDEBUG 警告）。

- [ ] **Step 9: 手动验证（foobar）**

重启 foobar2000，播放有 synced 歌词的曲目，指针置于桌面歌词窗口：
- 向下滚一格：切到下一句且音频跳到该句起点；向上滚：切到上一句。
- 触控板缓慢滑动：约每 40pt 跳一行，不会一次跳多行。
- 主面板歌词高亮在 ≤0.1s 内同步到同一句。
- 播放无 synced 歌词（纯文本/无歌词）的曲目滚动：无跳播、无异常。

- [ ] **Step 10: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm
git commit -m "桌面歌词滚轮改为逐行跳播"
```

---

### Task 3: 方向键逐行跳播（键盘焦点方案 A）

**Files:**
- Modify: `extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm`

**Interfaces:**
- Consumes: Task 2 的 `onSeekLineSteps`；既有 `_panel` 初始化、`DeskLyricsContent mouseDown:`。
- Produces: 新 `NSPanel` 子类 `DeskLyricsPanel`（`canBecomeKeyWindow==YES`）；`DeskLyricsContent` 的 `acceptsFirstResponder`/`keyDown:` 实现。

- [ ] **Step 1: 定义可取焦点的面板子类**

在 `DesktopLyricsController.mm` 中 `DeskLyricsContent` 的 `@interface` 之前（约 106 行注释上方）加：

```objc
// 允许成为 key 窗口以接收方向键；保持不成为 main，避免整体夺取应用主窗口地位。
@interface DeskLyricsPanel : NSPanel
@end

@implementation DeskLyricsPanel
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }
@end
```

- [ ] **Step 2: 面板改用子类**

将 `_panel = [[NSPanel alloc] initWithContentRect:initialFrame`（约 828 行）中的 `NSPanel` 改为 `DeskLyricsPanel`：

```objc
    _panel = [[DeskLyricsPanel alloc] initWithContentRect:initialFrame
        styleMask:NSWindowStyleMaskBorderless
              | NSWindowStyleMaskNonactivatingPanel
              | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
```

- [ ] **Step 3: 内容视图接受第一响应者并处理方向键**

在 `DeskLyricsContent` 的 `@implementation` 内、`scrollWheel:` 方法之后加：

```objc
- (BOOL)acceptsFirstResponder { return YES; }

- (void)keyDown:(NSEvent *)event {
    NSString *chars = event.charactersIgnoringModifiers;
    if (chars.length == 1) {
        unichar c = [chars characterAtIndex:0];
        if (c == NSUpArrowFunctionKey) {
            if (self.onSeekLineSteps) self.onSeekLineSteps(-1);
            return;
        }
        if (c == NSDownArrowFunctionKey) {
            if (self.onSeekLineSteps) self.onSeekLineSteps(1);
            return;
        }
    }
    [super keyDown:event];
}
```

- [ ] **Step 4: 点击窗口即取键盘焦点**

在 `DeskLyricsContent` 的 `- (void)mouseDown:(NSEvent *)event {`（约 309 行）方法体第一行插入：

```objc
    [self.window makeFirstResponder:self];
```

即改为：

```objc
- (void)mouseDown:(NSEvent *)event {
    [self.window makeFirstResponder:self];
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    DeskEdge edges = [self edgesForPoint:loc];
```

- [ ] **Step 5: 构建并安装**

Run:
```bash
cd ~/foo_openlyrics_mac/build && /opt/homebrew/bin/cmake --build . --target foo_openlyrics 2>&1 | tail -4 && cd ~/foo_openlyrics_mac && bash Scripts/install-component.sh 2>&1 | tail -2
```
Expected: 编译链接成功、安装完成，无错误。

- [ ] **Step 6: 手动验证（foobar）**

重启 foobar2000，播放有 synced 歌词的曲目：
- 单击桌面歌词窗口一次（取得键盘焦点），按下方向键：切到下一句并跳播；按上方向键：切到上一句。
- 主面板歌词同步高亮到同一句。
- 焦点在 foobar 主窗口（未点击歌词窗口）时，方向键仍归主窗口，不被歌词窗口截获。
- 点击歌词窗口后拖动仍能移动窗口、右键菜单仍正常。

- [ ] **Step 7: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm
git commit -m "桌面歌词支持方向键逐行跳播"
```

---

## 自查

- **Spec 覆盖**：交互定义→Task 2/3；核心跳播 `seekLyricLineBySteps:`→Task 2 Step 6；滚轮改造→Task 2；键盘方案 A→Task 3；主界面同步→依赖真实 seek，Task 2 Step 6 注释与手动验证覆盖；边界（非 synced/不可 seek/越界钳制/跳过无时标行）→Task 1 测试 + Task 2 方法守卫。全部有对应任务。
- **占位扫描**：无 TBD/TODO；每个改代码步骤均给出完整代码与锚点行号。
- **类型一致**：`seekTargetForLineStep`（int64_t 返回，-1 哨兵）、`onSeekLineSteps`（`void(^)(int)`）、`seekLyricLineBySteps:`（int 参数）在各任务引用一致；`steps` 符号语义（正=更晚/负=更早）在 Task 1 实现、Task 2 滚轮映射、Task 3 方向键映射三处一致。
