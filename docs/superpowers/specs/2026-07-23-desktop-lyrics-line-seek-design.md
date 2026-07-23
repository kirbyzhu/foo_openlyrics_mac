# 桌面歌词窗口：滚轮/方向键逐行跳播设计

日期：2026-07-23
状态：已确认方向，待实现

## 目标

焦点或指针在桌面歌词窗口时，滚轮一格或上/下方向键将当前歌词切到上一句/下一句，并把音频播放位置 seek 到该句的时间戳，歌词与声音一起跳，用于快速定位到某句。此行为替换原滚轮"调整偏移时间"的交互。偏移调整仍保留在右键菜单（±500/±1000/重置）与 Option+拖拽微调两处，不受影响。

## 交互定义

- 滚轮：传统鼠标每格步进 ±1 行；触控板精确滚动按像素阈值累积，累计达到阈值步进一行，避免一次手势跳多行。
- 方向键：上 → 上一句（更早），下 → 下一句（更晚）。滚轮方向一致：上滚更早，下滚更晚。
- 仅当当前歌词为 synced（含有效时标行）且当前曲目可 seek 时生效；否则忽略该输入。

## 组件与改动

改动集中在 `extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm`。

### 1. 逐行跳播核心

控制器新增 `-(void)seekLyricLineBySteps:(int)steps`：

1. 前置校验：`_currentLyricData.synced` 为真、存在时标行、且 `playback_control` 当前可 seek；任一不满足直接返回。
2. 取当前播放位置 `posMs = [self currentPositionMs]`，经 `openlyrics::SyncEngine::locate(_currentLyricData, posMs, _currentExtraOffsetMs)` 得当前行索引 `cur`。
3. 在**有时标的行**（`timeMs >= 0`，跳过如网易云署名等无时标 JSON 行）上按 `steps` 步进，结果钳制到 `[首个时标行, 末个时标行]`。若 `cur == -1`（尚未到首句）且 `steps > 0`，目标取首个时标行。
4. 计算 seek 目标：`seekMs = lines[target].timeMs - _currentLyricData.offsetMs - _currentExtraOffsetMs`，钳制 `>= 0`；调用 `playback_control::get()->playback_seek(seekMs / 1000.0)`。
5. seek 后立即 `[self tickSync]`，使桌面自身零延迟高亮目标行；不显示偏移 HUD。

时间换算依据 `SyncEngine::locate` 的判定式 `eff = posMs + data.offsetMs + extraOffsetMs`，令 `eff == lines[target].timeMs` 即得上式 seek 目标。

### 2. 滚轮改造（`DeskLyricsContent scrollWheel:`）

- 移除偏移累积与延迟提交计时器逻辑（`_scrollAccumMs`、`_scrollCommitTimer`、`onOffsetDelta`/`onOffsetCommit` 在滚轮路径的调用）。
- 传统滚轮（`hasPreciseScrollingDeltas == NO`）：每格 `deltaY` 符号决定方向，步进 ±1 行。
- 触控板精确滚动：累积像素 `deltaY` 到阈值 `kLineStepPixels`（拟定 40pt）触发一步并扣除阈值，连续手势可多步。
- 通过新增回调 `onSeekLineSteps(int steps)` 通知控制器调用 `-seekLyricLineBySteps:`。
- Option+拖拽微调偏移路径（`onOffsetDelta`/`onOffsetCommit`，`DesktopLyricsController.mm:378`）保持不变。

### 3. 键盘焦点与方向键（方案 A）

- 新增轻量 `NSPanel` 子类 `DeskLyricsPanel`，重写 `canBecomeKeyWindow` 返回 `YES`、`canBecomeMainWindow` 返回 `NO`；`_panel` 改用该子类，样式仍为 `Borderless | NonactivatingPanel`。
- `DeskLyricsContent` 重写 `acceptsFirstResponder` 返回 `YES`，实现 `keyDown:`：`NSUpArrowFunctionKey` → `onSeekLineSteps(-1)`，`NSDownArrowFunctionKey` → `onSeekLineSteps(1)`，其余按键交 `super`。
- 窗口成为 key 或内容视图 `mouseDown:` 时将其设为 first responder，使点击窗口后方向键即生效。
- 滚轮事件按窗口命中投递，不依赖键盘焦点，始终可用。
- 代价：点击桌面歌词窗口会取得键盘焦点（从 foobar 主窗口移交），符合"焦点在桌面歌词窗口时"的语义；NonactivatingPanel 保证不整体激活 App。

## 与主界面同步

桌面窗口执行的是真实 `playback_seek`，改变全局播放位置这一单一事实源。主面板 `LyricPanelController` 与桌面控制器均以 `kSyncTickInterval = 0.06s` 轮询 `playback_get_position()` 并 `SyncEngine::locate` 刷新高亮，故 seek 后两者在 ≤60ms 内自动同步，无需额外通知接线。滚轮与键盘两条路径都最终归于同一个 `seekLyricLineBySteps:`，同步行为一致。

## 边界与非改动

- 非 synced 歌词、不可 seek 曲目、无当前曲目：忽略滚轮/方向键跳播输入。
- 目标越界：钳制到首/末时标行，不回绕。
- 偏移的右键菜单项、Option+拖拽微调、窗口移动/缩放/边缘 resize/右键菜单等其余手势均不改动。
- 不触碰 `commitOffsetDelta`、`broadcastLyricChangedFromSelf` 等既有偏移落盘逻辑。

## 测试

- 核心步进/钳制逻辑与 seek 目标换算可抽为纯函数并加 gtest（给定行时标数组、当前行、steps、offset → 期望 seek 毫秒），覆盖：正常前后步、越界钳制、cur==-1 向后、跳过无时标行、offset 非零换算。
- 键盘焦点、滚轮阈值、面板子类等 AppKit 行为无单测，构建后在 foobar 手动验证：滚轮逐行跳、上下键逐行跳、主面板同步高亮、非 synced 歌词无响应。
