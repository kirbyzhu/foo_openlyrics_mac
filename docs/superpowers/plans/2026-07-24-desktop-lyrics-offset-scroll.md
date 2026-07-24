# 桌面歌词滚轮/方向键调整偏移 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 撤销逐行 seek 实现，改为用滚轮/上下方向键调整当前歌词偏移量（歌词-歌曲时间差），不改变播放进度。

**Architecture:** 撤销 `SyncEngine::seekTargetForLineStep` 与相关测试；`scrollWheel:` 恢复偏移累积+HUD+0.8s 落盘并反转方向符号；累积逻辑抽为 `DeskLyricsContent` 私有方法 `applyOffsetDelta:`，滚轮与 `keyDown:` 方向键共用；保留 a89d0d3 已加的键盘焦点基础设施（`DeskLyricsPanel`、`acceptsFirstResponder`、`mouseDown:` 取焦点）。

**Tech Stack:** Objective-C++ / AppKit / foobar2000 SDK / C++17 核心 / GoogleTest。

## Global Constraints

- 方向映射（滚轮与方向键统一）：上滚/上方向键 → 歌词延后 → `deltaMs < 0`；下滚/下方向键 → 歌词提前 → `deltaMs > 0`。依据 `SyncEngine::locate` 的 `eff = positionMs + offsetMs + extraOffsetMs`，offset 增大使歌词提前。
- 滚轮粒度恢复改动前：触控板 `10ms/px`、鼠标滚轮每格 `kScrollMsPerWheelTick=100ms`；仅相对改动前反转符号。
- 方向键步进 `kArrowOffsetStepMs=100`，连按累积。
- 落盘沿用既有 `onOffsetCommit → commitOffsetDelta:`，停止输入 0.8s（`kScrollCommitDelay`）后提交；实时预览经 `onOffsetDelta`；HUD 经 `showOffsetHudWithDelta:`。
- 不改动：右键菜单 ±500/±1000/重置偏移、Option+拖拽微调（`_scrubDeltaMs` 路径）、`commitOffsetDelta`、`broadcastLyricChangedFromSelf`、窗口移动/缩放/边缘 resize。

---

### Task 1: 撤销 SyncEngine 逐行 seek 与测试

**Files:**
- Modify: `extensions/foo_openlyrics_mac/core/sync/SyncEngine.h`
- Modify: `extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp`
- Modify: `tests/test_sync_engine.cpp`

**Interfaces:**
- Consumes: 无。
- Produces: 无（纯删除，恢复 `SyncEngine` 到仅有 `locate` 的状态）。

- [ ] **Step 1: 删除头文件声明**

编辑 `extensions/foo_openlyrics_mac/core/sync/SyncEngine.h`，删除 `locate(...)` 声明之后新增的整段：

```cpp

    // 从当前播放位置按 steps 步进到目标时标行，返回应 seek 到的播放位置（毫秒，>=0）。
    // steps>0 向后（更晚），<0 向前（更早）。目标钳制到 [首个时标行, 末个时标行]。
    // data 非 synced 或无时标行时返回 -1（调用方忽略）。
    static int64_t seekTargetForLineStep(const LyricData& data, int64_t positionMs,
                                         int steps, int64_t extraOffsetMs = 0);
```

删除后 `locate(...)` 声明紧接类结束 `};`。

- [ ] **Step 2: 删除实现定义**

编辑 `extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp`，删除 `locate(...)` 之后、`}  // namespace openlyrics` 之前的整个 `seekTargetForLineStep` 函数定义（从 `int64_t SyncEngine::seekTargetForLineStep(` 到其结束 `}`），并删除顶部第 2 行 `#include <vector>`。

- [ ] **Step 3: 删除新增测试**

编辑 `tests/test_sync_engine.cpp`，删除末尾追加的 11 个用例：`TEST(SyncEngine, StepForwardFromCurrentLine)` 到 `TEST(SyncEngine, StepNoTimedLinesReturnsMinusOne)` 全部整块删除（保留其上方原有的 `SkipsInterspersedUntimedLines` 用例为文件最后一个）。

- [ ] **Step 4: 构建并运行测试确认通过**

Run: `cd ~/foo_openlyrics_mac/build && cmake --build . --target core_tests 2>&1 | tail -3 && ./core_tests 2>&1 | tail -2`
Expected: 编译通过，`[  PASSED  ] 224 tests.`（seek 前基线 224）。

- [ ] **Step 5: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/core/sync/SyncEngine.h extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp tests/test_sync_engine.cpp
git commit -m "撤销逐行 seek 目标换算与测试"
```

---

### Task 2: 滚轮恢复偏移调整并反转方向

**Files:**
- Modify: `extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm`

**Interfaces:**
- Consumes: 既有 `showOffsetHudWithDelta:`、`onOffsetDelta`、`onOffsetCommit`、`fadeHud`。
- Produces: `DeskLyricsContent` 新私有方法 `-(void)applyOffsetDelta:(int64_t)deltaMs;`（供 Task 3 方向键复用）；恢复 ivar `_scrollAccumMs`/`_scrollCommitTimer`、常量 `kScrollMsPerWheelTick`/`kScrollCommitDelay`、`scrollAccumMs` 属性。移除 `onSeekLineSteps` 属性、`seekLyricLineBySteps:` 方法与其接线。

- [ ] **Step 1: 恢复滚轮常量，删除像素阈值常量**

将文件顶部常量（约 39 行）：

```objc
static const CGFloat kScrollPixelsPerLine = 40.0;    // 触控板精确滚动每累积此像素步进一行
```

替换为：

```objc
static const int64_t kScrollMsPerWheelTick = 100;    // 滚轮每刻度偏移量（毫秒）
static const NSTimeInterval kScrollCommitDelay = 0.8; // 滚轮停止后多久自动提交偏移
static const int64_t kArrowOffsetStepMs = 100;       // 方向键每次调整的偏移量（毫秒）
```

- [ ] **Step 2: 删除 onSeekLineSteps 属性**

在 `DeskLyricsContent` 的 `@interface`（约 120 行）删除：

```objc
@property(nonatomic, copy) void (^onSeekLineSteps)(int steps);
```

- [ ] **Step 3: 恢复 scrollAccumMs 属性声明**

在 `DeskLyricsContent` 的 `- (void)showMessage:(NSString *)text;` 之后、`@end`（约 142-143 行）之前加回：

```objc

// 当前滚轮累积的偏移量（毫秒），供外部在提交后清零。
@property(nonatomic, assign) int64_t scrollAccumMs;
```

- [ ] **Step 4: 恢复滚轮 ivar**

将 ivar 块中（约 170 行）：

```objc
    CGFloat _scrollLineAccumPx;     // 触控板精确滚动的像素累积，达阈值步进一行
```

替换为：

```objc
    int64_t _scrollAccumMs;         // 滚轮累积偏移量
    NSTimer *_scrollCommitTimer;    // 滚轮停止后延迟提交
```

- [ ] **Step 5: 重写 scrollWheel: 并新增 applyOffsetDelta:**

将 `scrollWheel:` 整个方法体（约 497-525 行，从 `- (void)scrollWheel:(NSEvent *)event {` 到其结束 `}`）替换为下面两个方法：

```objc
- (void)scrollWheel:(NSEvent *)event {
    // 拖拽/缩放/边缘调整进行中时不响应，交回系统
    if (_isDragging || _isScrubbing || _isResizing || _isCmdResizing) {
        [super scrollWheel:event];
        return;
    }

    CGFloat dy = event.deltaY;
    if (fabs(dy) < 0.01) return;

    int64_t deltaMs;
    if (event.hasPreciseScrollingDeltas) {
        // 触控板：每像素 ≈ 10ms；上滑(dy>0)->歌词延后(deltaMs<0)
        deltaMs = static_cast<int64_t>(-dy * 10.0);
    } else {
        // 传统鼠标滚轮：每刻度 kScrollMsPerWheelTick；上滚(dy>0)->歌词延后
        deltaMs = static_cast<int64_t>(-dy * kScrollMsPerWheelTick);
    }

    [self applyOffsetDelta:deltaMs];
}

// 累积一次偏移增量：实时 HUD + 实时同步 + 重置 0.8s 延迟提交计时器。滚轮与方向键共用。
- (void)applyOffsetDelta:(int64_t)deltaMs {
    if (deltaMs == 0) return;

    _scrollAccumMs += deltaMs;

    // 实时 HUD 反馈，复用 Option+拖拽的显示逻辑
    [self showOffsetHudWithDelta:_scrollAccumMs];

    // 实时同步到歌词视图
    if (self.onOffsetDelta) self.onOffsetDelta(_scrollAccumMs);

    // 重置延迟提交计时器
    [_scrollCommitTimer invalidate];
    _scrollCommitTimer = [NSTimer scheduledTimerWithTimeInterval:kScrollCommitDelay
                                                         repeats:NO
                                                           block:^(NSTimer *timer) {
        if (self.onOffsetCommit && self->_scrollAccumMs != 0) {
            self.onOffsetCommit(self->_scrollAccumMs);
        }
        self->_scrollAccumMs = 0;
        [self fadeHud];
    }];
}

- (int64_t)scrollAccumMs { return _scrollAccumMs; }
- (void)setScrollAccumMs:(int64_t)v { _scrollAccumMs = v; }
```

- [ ] **Step 6: 删除 seekLyricLineBySteps: 方法**

删除 `DesktopLyricsController` 中 `-(void)seekLyricLineBySteps:(int)steps { ... }` 整个方法（约 1387 行，位于 `currentPositionMs` 之后、`#pragma mark - 偏移拖拽提交` 之前），连同其上方注释行 `// 滚轮/方向键逐行跳播：seek 到目标时标行，主面板由其 0.06s tick 自动跟随。`。

- [ ] **Step 7: 删除 onSeekLineSteps 接线**

删除控制器 setup 块中（约 906 行，`onOffsetCommit` 赋值之后）的整段：

```objc
    _contentView.onSeekLineSteps = ^(int steps) {
        __typeof__(self) strongSelf = weakSelf;
        if (strongSelf == nil) return;
        [strongSelf seekLyricLineBySteps:steps];
    };
```

（Task 3 会新增方向键接线；此处先删除以消除对已删方法的引用。）

- [ ] **Step 8: 临时改 keyDown 消除对 onSeekLineSteps 的引用**

此时 `keyDown:` 仍引用已删的 `onSeekLineSteps`。将 `keyDown:` 方法体（约 529-543 行）中两处 `if (self.onSeekLineSteps) self.onSeekLineSteps(-1);` 与 `...(1);` 分别改为 `[self applyOffsetDelta:-kArrowOffsetStepMs];` 与 `[self applyOffsetDelta:kArrowOffsetStepMs];`。即 `keyDown:` 变为：

```objc
- (void)keyDown:(NSEvent *)event {
    NSString *chars = event.charactersIgnoringModifiers;
    if (chars.length == 1) {
        unichar c = [chars characterAtIndex:0];
        if (c == NSUpArrowFunctionKey) {
            [self applyOffsetDelta:-kArrowOffsetStepMs];   // 上：歌词延后
            return;
        }
        if (c == NSDownArrowFunctionKey) {
            [self applyOffsetDelta:kArrowOffsetStepMs];    // 下：歌词提前
            return;
        }
    }
    [super keyDown:event];
}
```

- [ ] **Step 9: 全局检查无残留引用**

Run: `cd ~/foo_openlyrics_mac && grep -rn "onSeekLineSteps\|seekLyricLineBySteps\|_scrollLineAccumPx\|kScrollPixelsPerLine\|seekTargetForLineStep" extensions/ tests/`
Expected: 无输出（全部残留已清除）。

- [ ] **Step 10: 构建并安装**

Run:
```bash
cd ~/foo_openlyrics_mac/build && /opt/homebrew/bin/cmake --build . --target foo_openlyrics 2>&1 | tail -4 && cd ~/foo_openlyrics_mac && bash Scripts/install-component.sh 2>&1 | tail -2
```
Expected: 链接成功、安装完成，仅既有 pfc NDEBUG 警告。

- [ ] **Step 11: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm
git commit -m "桌面歌词滚轮恢复为偏移调整并反转方向"
```

---

### Task 3: 方向键与手动验证

Task 2 Step 8 已把 `keyDown:` 接到 `applyOffsetDelta:`，方向键功能代码已完成。本任务仅做手动验证与收尾提交（无独立代码改动，若 Task 2 已提交则本任务只含验证）。

**Files:**
- Verify only: `extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm`

**Interfaces:**
- Consumes: Task 2 的 `applyOffsetDelta:`；a89d0d3 保留的 `DeskLyricsPanel`/`acceptsFirstResponder`/`mouseDown:` 取焦点。
- Produces: 无新代码。

- [ ] **Step 1: 确认键盘焦点基础设施仍在位**

Run: `cd ~/foo_openlyrics_mac && grep -n "DeskLyricsPanel\|acceptsFirstResponder\|makeFirstResponder" extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm`
Expected: `DeskLyricsPanel` 子类定义与 `_panel` 用它、`acceptsFirstResponder` 返回 YES、`mouseDown:` 首行 `makeFirstResponder:self` 均在。

- [ ] **Step 2: 手动验证（foobar）**

重启 foobar2000，播放一首歌词与人声不同步的曲目，指针置于桌面歌词窗口：
- 上滚：歌词相对歌曲延后，HUD 显示偏移变化；下滚：歌词提前。
- 触控板缓慢滑动：偏移平滑变化，方向与滚轮一致。
- 停止滚动约 0.8s：偏移落盘（切歌再回来仍保留）。
- 单击歌词窗口取得焦点后，上方向键歌词延后、下方向键歌词提前，每次约 100ms，连按累积。
- 主面板歌词随偏移更新；右键 ±500/±1000/重置偏移、Option+拖拽微调、窗口拖动/缩放均正常。
- 焦点在 foobar 主窗口（未点击歌词窗口）时方向键仍归主窗口。
- 若上滚方向与预期相反（受系统"自然滚动"设置影响），将 Step 5 两处 `-dy` 改回 `dy`（去掉负号）重编即可。

- [ ] **Step 3: 更新旧计划状态并提交（如有改动）**

若手动验证需微调符号则一并提交；否则本任务无新提交。旧的逐行跳播 spec/plan 保留在库中作为历史记录，不删除。

---

## 自查

- **Spec 覆盖**：撤销 seek→Task 1 + Task 2 Step 6/7；滚轮恢复偏移并反转→Task 2 Step 5；`applyOffsetDelta:` 共享方法→Task 2 Step 5；方向键接偏移→Task 2 Step 8 + Task 3；保留键盘焦点基础设施→Task 3 Step 1；方向映射（上=延后）→Global Constraints + Step 5/8 注释；落盘/HUD 复用既有→Task 2 Step 5。全部有对应任务。
- **占位扫描**：无 TBD/TODO；每个改代码步骤均给出完整代码与锚点行号。
- **类型一致**：`applyOffsetDelta:`（int64_t 参数）在 Step 5 定义、Step 8 与滚轮调用一致；`kArrowOffsetStepMs`（int64_t=100）在 Step 1 定义、Step 8 使用一致；删除项 `onSeekLineSteps`/`seekLyricLineBySteps:`/`seekTargetForLineStep`/`_scrollLineAccumPx`/`kScrollPixelsPerLine` 由 Step 9 grep 兜底确认全清。
