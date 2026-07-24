# 桌面歌词逐句手动打轴（tap-to-sync）设计

日期：2026-07-24
状态：已确认方向，待实现

## 背景

歌词与人声不同步时，整体平移 offset 无法处理逐句错位。本设计把桌面歌词窗口的滚轮与上/下方向键改为**逐句手动打轴**：一边听歌一边手动把每句时标对齐到人声，结果直接写回 lrc 时间戳。替换此前"滚轮/方向键调整整体 offset"的交互（该交互见已取代的 2026-07-24-desktop-lyrics-offset-scroll-design.md）。整体 offset 仍可经右键菜单 ±500/±1000/重置调整。

## 目标

- 桌面歌词窗口获得键盘焦点或指针在其上时，用上/下方向键或滚轮逐句打轴，音乐始终正常播放、不 seek。
- 每次推进立即把新时间戳写回 lrc，主面板高亮与桌面同步到同一句。
- 上键/上滚可回退纠错，撤销刚打的时间戳。

## 交互定义

### 两个状态

- 自动跟随（默认）：高亮随播放位置自动前进，即现有 `tickSync → SyncEngine::locate` 行为。
- 打轴模式：高亮冻结在手动选定行，由键盘/滚轮推进；tickSync 不再用播放位置定位。

### 进入 / 退出

- 右键（→）：把当前高亮句锁为锚点，进入打轴模式。打轴中再按右键＝把当前高亮句重设为锚点（幂等，不退出）。
- 退出：桌面歌词窗口失去键盘焦点（点击窗口外）或切歌（`playbackHubDidChange` 新曲目）时，退出打轴、恢复自动跟随。

### 打轴中的操作（音乐正常播放，绝不 seek）

- 下键 / 下滚：从当前锚点找**下一个有时标行** next（跳过无时标行），把 `next.timeMs = 当前播放位置`，重建 sourceText 并立即写回 lrc；高亮移到 next，next 成为新锚点。已在最后一个有时标行时停住不动。
- 上键 / 上滚：从当前锚点找**上一个有时标行** prev，撤销当前锚点行本次打轴（恢复其原时间戳），重建 sourceText 并写回 lrc；高亮退回 prev，prev 成为新锚点。已在第一个有时标行（或锚点为 -1）时停住不动。
- 上滚＝上键、下滚＝下键，完全等价（下滚 `deltaY<0`→前进，上滚 `deltaY>0`→回退）。触控板精确滚动按像素阈值 `kTapPixelsPerStep=40` 累积，避免一次手势跳多步；传统滚轮每格一步。

### 边界规则（均按用户确认取 A）

- 无时标行（如网易云署名）：打轴推进时跳过，只在有时标行之间移动。
- 到最后一个有时标行后再按下键：停住不动，不结束、不回绕。
- 锚点初始为 -1（尚未到首句）时，第一次下键把首个时标行时标设为当前位置并高亮它。

## 组件与改动

### 1. 纯函数：相邻时标行查找（可测）

`extensions/foo_openlyrics_mac/core/sync/SyncEngine.{h,cpp}` 新增静态纯函数：

```cpp
// 从 fromLine（data.lines 下标，-1 表示尚未到首句）沿 dir(+1 前进/-1 后退) 找相邻的有时标行下标。
// 跳过 timeMs<0 的行。到端点无更多时标行时返回 fromLine（即停住）。
// 特例：fromLine==-1 且 dir==+1 返回首个时标行；无任何时标行返回 -1。
static int adjacentTimedLine(const LyricData& data, int fromLine, int dir);
```

gtest 覆盖：正常前进/后退、跳过中间无时标行、末行前进停住、首行后退停住、fromLine==-1 前进取首行、无时标行返回 -1。

### 2. PlaybackHub：手动高亮广播

`extensions/foo_openlyrics_mac/platform/PlaybackBridge.{h,mm}`：

- 协议 `PlaybackHubObserving` 新增可选方法 `- (void)playbackHubManualHighlightDidChange:(NSInteger)lineIndex;`，`lineIndex>=0` 表示手动高亮该行，`-1` 表示清除手动高亮、恢复自动跟随。
- `PlaybackHub` 新增 `- (void)notifyManualHighlightLine:(NSInteger)lineIndex;`，遍历观察者调用上述可选方法（存在性用 `respondsToSelector:` 判断，与既有 `notifyLyricChanged` 同风格）。

### 3. DesktopLyricsController：打轴状态机（发起方）

`extensions/foo_openlyrics_mac/ui/DesktopLyricsController.mm`：

- 撤销此前 dac87be 的滚轮/方向键调 offset 逻辑：删除 `applyOffsetDelta:`、`scrollWheel:` 内的偏移累积、`kScrollMsPerWheelTick`/`kScrollCommitDelay`/`kArrowOffsetStepMs`/`_scrollAccumMs`/`_scrollCommitTimer`/`scrollAccumMs` 属性。保留键盘焦点基础设施 `DeskLyricsPanel`、`acceptsFirstResponder`、`mouseDown:` 首行 `makeFirstResponder:self`。
- 新增 ivar：`BOOL _tapActive`（是否打轴中）、`int _tapAnchorLine`（当前手动高亮行，data.lines 下标，-1 合法）、`std::vector<std::pair<int,int64_t>> _tapUndo`（每次下键前保存 (行下标, 原 timeMs)）、`CGFloat _tapAccumPx`（触控板像素累积）。常量 `kTapPixelsPerStep=40`。
- 新增 `DeskLyricsContent` 回调属性：`onTapAnchor`（右键锁定）、`onTapStep`（`void(^)(int dir)`，±1）。
- `scrollWheel:` 改为把 `deltaY` 折算成 `dir`（触控板按 `kTapPixelsPerStep` 累积、传统滚轮每格 ±1；`deltaY<0`→dir=+1 前进）后 `onTapStep(dir)`。
- `keyDown:` 改为：右键 `NSRightArrowFunctionKey → onTapAnchor`；下键 `NSDownArrowFunctionKey → onTapStep(+1)`；上键 `NSUpArrowFunctionKey → onTapStep(-1)`；其余交 `super`。
- 控制器方法：
  - `-(void)tapAnchorAtCurrent`：`_tapActive=YES`，`_tapAnchorLine = SyncEngine::locate(_currentLyricData, 当前位置, _currentExtraOffsetMs).lineIndex`，清空 `_tapUndo`，`[self freezeHighlightTo:_tapAnchorLine]` 并 `notifyManualHighlightLine:_tapAnchorLine`。
  - `-(void)tapStep:(int)dir`：仅当 `_tapActive` 且歌词非空时生效。
    - dir>0：`next = SyncEngine::adjacentTimedLine(_currentLyricData, _tapAnchorLine, +1)`；`next==_tapAnchorLine` 则停住返回；`_tapUndo.push_back({next, _currentLyricData.lines[next].timeMs})`；`_currentLyricData.lines[next].timeMs = 当前位置`；`_tapAnchorLine=next`。
    - dir<0：`_tapUndo` 非空且顶元素行==`_tapAnchorLine` 时，恢复该行原 timeMs 并弹栈；`prev = adjacentTimedLine(_currentLyricData, _tapAnchorLine, -1)`；`_tapAnchorLine=prev`。
    - 之后统一：重建 `sourceText`、写盘、刷新桌面高亮、广播（见下）。
  - `-(void)freezeHighlightTo:(int)line`：构造 `SyncResult{line, 0.0}` 交 `_lyricView setSyncResult:`，避免下一次 `tickSync` 用位置覆盖。
  - `-(void)persistTapAndBroadcast`：`_currentLyricData.sourceText = LrcSerializer::serialize(_currentLyricData)`；`_currentLyricData.synced=true`；用 `LyricStore::forceSave(当前 track meta, _currentLyricData)` 写盘；`broadcastLyricChangedFromSelf`（主面板重载新时标）；`notifyManualHighlightLine:_tapAnchorLine`（主面板高亮同一句）。track meta 取自 `[[PlaybackHub sharedHub] currentTrack]`。
- `tickSync` 改：`if (_tapActive) { [self freezeHighlightTo:_tapAnchorLine]; return; }` 置于原 locate 之前。
- 退出打轴：窗口 `resignKeyWindow`（或内容视图失焦）与 `playbackHubDidChange`（新曲目）时置 `_tapActive=NO`、`_tapUndo.clear()`、`notifyManualHighlightLine:-1`。

### 4. LyricPanelController：手动高亮覆盖（接收方）

`extensions/foo_openlyrics_mac/ui/LyricPanelController.mm`：

- 新增 ivar `NSInteger _manualHighlightLine`（-1=自动，初值 -1）。
- 实现 `- (void)playbackHubManualHighlightDidChange:(NSInteger)lineIndex`：`_manualHighlightLine = lineIndex`，立即 `[self.lyricView setSyncResult:...]`（`lineIndex>=0` 用 `{(int)lineIndex,0.0}`，否则回到 `tickSync` 逻辑刷新一次）。
- `tickSync` 改：`if (_manualHighlightLine >= 0) { [self.lyricView setSyncResult:SyncResult{(int)_manualHighlightLine, 0.0}]; return; }` 置于原 locate 之前。
- `playbackHubLyricDidChange` 重载歌词后不清 `_manualHighlightLine`（桌面退出时会单独广播 -1 清除）。

## 主界面同步

桌面每次打轴动作后：写盘 → `broadcastLyricChangedFromSelf`（主面板从磁盘重载带新时标的歌词，数据同步）→ `notifyManualHighlightLine:_tapAnchorLine`（主面板用手动索引高亮同一句，视觉同步）。打轴期间主面板 tickSync 因 `_manualHighlightLine>=0` 不按播放位置定位，与桌面冻结在同一句；退出打轴时桌面广播 -1，主面板恢复自动跟随。

## 边界与非改动

- 不改动：右键菜单 ±500/±1000/重置偏移、Option+拖拽微调、窗口移动/缩放/边缘 resize。整体 offset 仍由这些入口调整。
- 非 synced（无任何时标行）歌词：`adjacentTimedLine` 返回 -1，`tapStep:` 无有效目标，打轴不产生改动（不报错）。
- 打轴写盘复用既有 `LyricStore::forceSave` 与 `broadcastLyricChangedFromSelf` 的抑制自重载机制，避免写盘触发自身 `playbackHubLyricDidChange` 回读覆盖内存。

## 测试

- 纯函数 `adjacentTimedLine` 加 gtest（见组件 1），核心套件在 224 基础上增若干用例。
- 打轴状态机、写盘、双面板高亮同步为 AppKit + SDK 交互，构建安装后在 foobar 手动验证：
  - 右键锁定当前句，桌面与主面板均冻结高亮在该句。
  - 下键/下滚逐句推进，时标写为按键时刻播放位置；切歌再回来时标已更新。
  - 上键/上滚回退并撤销刚打的时标。
  - 主面板高亮全程与桌面同一句。
  - 点击窗口外或切歌后，两面板恢复随播放自动跟随。
  - 无时标行被跳过；到末句下键停住。
