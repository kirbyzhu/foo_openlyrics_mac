# 桌面歌词标题栏与高亮换行修复 设计

日期 2026-07-23
组件 foo_openlyrics_mac，版本 0.3.0 → 0.4.0

## 背景

透明桌面歌词窗口缺少当前曲目信息；高亮加粗行放大后被垂直截断，显示不完整。

## 需求一 顶部标题栏

单行「歌名 — 艺术家」，固定不滚动，超长省略。

### 归属与接口

标题在 `LyricView` 内实现，作为顶部固定信息区，复用其字体/颜色/对齐配置。
LyricView 不重写 `mouseDown:`，鼠标事件沿响应链上传 `DeskLyricsContent`，拖动/缩放/右键不受影响。

新增接口 `- (void)setTitleText:(NSString *)text;`。
Controller 在 `handleTrackChanged` 拼装标题（缺 artist 只显示 title；无曲目传空），经此接口下发。

### 绘制

- 标题绘制于顶部矩形 `(0, 0, width, titleH)`，不受 `_scrollOffset` 影响，天然不滚动。
- 单行，`NSLineBreakByTruncatingTail` 省略号，按现有 alignment 对齐，normalColor，字号取歌词 `fontSize`，常规体。
- 歌词区下移到 `titleH` 以下：绘制歌词前 `NSRectClip` 到 `(0, titleH, width, height - titleH)`，防止上滚歌词覆盖标题；`drawStaticLinesInBounds` / `drawSyncedLinesInBounds` 的 y 起点与可用高度相应减 `titleH`；占位「无歌词」居中于歌词区。
- `titleText` 为空时 `titleH = 0`，布局回退为纯歌词。

### 可关闭

偏好设置「桌面歌词」tab 增加复选框「显示标题栏」，对应 `DeskLyricsConfig.showTitle`（默认 true），纳入 JSON 序列化/校验。
关闭时 Controller 传空标题（`titleH = 0`）。

## 需求二 高亮加粗换行修复

### 根因

`LyricView.mm` 的 `computeRowHeightsIfNeeded` 用 `_normalAttrLines[i]`（普通字体）测行高，但高亮行字体放大 `highlightScale` 倍。绘制时高亮行 `rowRect` 高度仍为普通行高，放大加粗行被垂直截断。

### 修复

行高统一改用 `_highlightAttrLines[i]`（放大版）测量。字符换行 `NSLineBreakByCharWrapping` 已开启，无需改动。
任何行加粗都不超出等高 `rowRect`、不截断换行；滚动累加基于统一行高，`computeTargetScrollOffsetForResult` 与 `setFrameSize` 依赖同一 `_rowHeights`，自动一致。
代价：非高亮行行距略大，已确认可接受。

## 影响范围

- `core/config/AppConfig.h` / `AppConfig.cpp`：新增 `showTitle` 字段、JSON 读写、校验。
- `ui/LyricView.h` / `LyricView.mm`：`setTitleText:`、标题绘制、歌词区下移与 clip、行高改按高亮字体。
- `ui/DesktopLyricsController.mm`：`handleTrackChanged` 拼装并下发标题，`applyDeskLyricsDisplay` / `reloadConfig` 依 `showTitle` 决定标题内容。
- `ui/PreferencesViewController.mm`：「显示标题栏」复选框、handler、populate。
- `platform/component_entry.mm`：版本 0.3.0 → 0.4.0。

## 测试

- `AppConfig.showTitle` 的 JSON 序列化/默认值/向后兼容：单元测试覆盖。
- 标题绘制、歌词区下移、行高换行：依赖 AppKit 运行时，构建 + 部署后人工验证。
