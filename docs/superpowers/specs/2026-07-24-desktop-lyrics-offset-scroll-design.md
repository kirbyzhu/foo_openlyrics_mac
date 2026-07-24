# 桌面歌词窗口：滚轮/方向键调整歌词-歌曲时间差设计

日期：2026-07-24
状态：已被取代（滚轮/方向键改为逐句手动打轴，见 2026-07-24-desktop-lyrics-tap-sync-design.md）。本文档仅存历史。

## 背景

2026-07-23 的逐行跳播实现（seek 音频到上/下句）经手动验证与真实需求不符。用户的需求是：焦点或指针在桌面歌词窗口时，用滚轮或上/下方向键调整**当前歌词的偏移量（offset，歌词与歌曲的时间差）**，用于把不同步的歌词对齐到人声，不改变播放进度。本设计撤销 seek 实现，恢复并统一为偏移调整交互，并新增方向键支持。

## 目标

- 滚轮：调整当前歌词偏移量，粒度恢复改动前行为（触控板按像素 10ms/px、鼠标滚轮每格 100ms），实时 HUD 反馈，停止滚动 0.8s 后自动落盘。
- 方向键：上/下每按一次调整 ±100ms，连按累积，复用与滚轮相同的累积+HUD+延迟落盘路径。
- 撤销逐行 seek 相关代码与测试。

## 交互定义

方向映射（滚轮与方向键统一）：

- 上滚 / 上方向键 → 歌词延后（歌词相对歌曲往后挪，用于歌词比人声唱得早时）→ 内部 offset 减小。
- 下滚 / 下方向键 → 歌词提前（歌词相对歌曲往前挪，用于歌词比人声慢时）→ 内部 offset 增大。

offset 语义依据 `SyncEngine::locate` 的 `eff = positionMs + offsetMs + extraOffsetMs`：offset 增大使行时标更早被"命中"，即歌词提前。故"歌词提前"对应 `deltaMs > 0`，"歌词延后"对应 `deltaMs < 0`。

改动前原滚轮为"上滚→offset 增大→歌词提前"，与新方向相反，故本设计将滚轮符号一并反转，使滚轮与方向键方向一致。

生效条件：与原偏移调整一致，`_currentLyricData.lines` 非空即可（`commitOffsetDelta:` 内已守卫空歌词）。无 synced 与否不限制——偏移对无时标歌词无可见效果但不产生异常。

## 组件与改动

改动集中在 `extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm`；并撤销 `core/sync/SyncEngine.{h,cpp}` 与 `tests/test_sync_engine.cpp` 的 seek 新增。

### 1. 撤销逐行 seek

- 删除 `SyncEngine::seekTargetForLineStep` 声明与定义、`SyncEngine.cpp` 的 `#include <vector>`、`tests/test_sync_engine.cpp` 新增的 11 个 `SyncEngine.Step*` 用例。
- 删除 `DeskLyricsContent` 的 `onSeekLineSteps` 属性、`DesktopLyricsController` 的 `seekLyricLineBySteps:` 方法及其在 setup 块的接线。

### 2. 恢复滚轮偏移调整（并反转方向）

- 恢复常量 `kScrollMsPerWheelTick = 100`、`kScrollCommitDelay = 0.8`；删除 `kScrollPixelsPerLine`。
- 恢复 ivar `_scrollAccumMs`、`_scrollCommitTimer`；删除 `_scrollLineAccumPx`。恢复 `scrollAccumMs` 属性与其 getter/setter。
- `scrollWheel:` 恢复为累积 `_scrollAccumMs` → `showOffsetHudWithDelta:` → `onOffsetDelta` 实时同步 → 0.8s 后经 `onOffsetCommit` 落盘并清零。
- 与改动前唯一差异：deltaMs 符号反转。触控板 `deltaMs = (int64_t)(-dy * 10.0)`，鼠标滚轮 `deltaMs = (int64_t)(-dy * kScrollMsPerWheelTick)`，使上滚 → deltaMs < 0 → 歌词延后。

### 3. 抽出共享偏移应用方法

- 新增 `DeskLyricsContent` 私有方法 `-(void)applyOffsetDelta:(int64_t)deltaMs`，封装"累积 `_scrollAccumMs` + HUD + `onOffsetDelta` + 重置 0.8s 提交计时器"这段逻辑。
- `scrollWheel:` 算出带符号的 `deltaMs` 后调用 `[self applyOffsetDelta:deltaMs]`。

### 4. 方向键接偏移（保留 Task 3 键盘焦点基础设施）

- 保留改动前已加入的 `DeskLyricsPanel`（`canBecomeKeyWindow=YES`、`canBecomeMainWindow=NO`）、`_panel` 用该子类、`acceptsFirstResponder` 返回 YES、`mouseDown:` 首行 `makeFirstResponder:self`。
- `keyDown:` 改为：`NSUpArrowFunctionKey → [self applyOffsetDelta:-100]`、`NSDownArrowFunctionKey → [self applyOffsetDelta:100]`，其余按键交 `super`。步进常量 `kArrowOffsetStepMs = 100`。

## 与主界面同步

偏移的实时预览经 `onOffsetDelta` 推给桌面歌词视图；落盘经 `onOffsetCommit → commitOffsetDelta:`，后者写回 `_currentLyricData.offsetMs`、保存并 `broadcastLyricChangedFromSelf` 通知主面板重载，与既有 Option+拖拽路径完全一致，无需新增接线。

## 边界与非改动

- 不改动：右键菜单 ±500/±1000/重置偏移、Option+拖拽微调（`onOffsetDelta`/`onOffsetCommit`/`showOffsetHudWithDelta`/`_scrubDeltaMs`）、`commitOffsetDelta`、`broadcastLyricChangedFromSelf`、窗口移动/缩放/边缘 resize/右键菜单。
- 拖拽/缩放/边缘调整进行中时滚轮交回系统（`super`），与改动前一致。

## 测试

- 无新增核心纯函数（复用既有 `commitOffsetDelta`、`SyncEngine::locate`）。删除 seek 后核心测试套件回到 224 项，应全绿。
- 滚轮/方向键为 AppKit 交互，构建安装后在 foobar 手动验证：
  - 上滚歌词延后、下滚歌词提前，HUD 显示偏移变化；停止 0.8s 后偏移持久化。
  - 单击歌词窗口取得焦点后，上方向键歌词延后、下方向键歌词提前，每次 100ms。
  - 主面板歌词随之更新；右键菜单偏移项、Option+拖拽、窗口拖动/缩放不受影响。
  - 焦点在 foobar 主窗口时方向键仍归主窗口。
