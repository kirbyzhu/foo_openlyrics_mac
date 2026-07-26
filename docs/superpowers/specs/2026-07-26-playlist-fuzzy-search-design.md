# 播放列表模糊搜索定位设计

日期：2026-07-26
状态：已确认方向，待实现

## 背景

foobar2000 主界面缺少快速定位当前播放列表内歌曲的手段。用户希望：在活动播放列表中做模糊查询，按快捷键 Cmd+F 弹出搜索框，输入后即时过滤，回车定位到对应歌曲；并在歌单右键菜单中提供入口且标注键位；模糊匹配支持中文拼音，含多音字。

组件现有形态：一个 `ui_element_mac` 歌词面板 + 偏好页 + `play_callback_static` 桥（见 `platform/component_entry.mm`、`platform/PlaybackBridge.mm`）。本功能与歌词无关，但并入同一 `foo_openlyrics` bundle。

SDK 能力（`SDK-2025-03-07`）已核实：

- 播放列表：`playlist.h` 提供 `activeplaylist_get_item_count` / `activeplaylist_get_item_handle` / `activeplaylist_set_focus_by_handle`（第 385 行，基于 handle，抗重排）/ `activeplaylist_set_selection_single` / `activeplaylist_ensure_visible`。
- 元数据：`metadb_handle::get_info_ref`（与 `PlaybackBridge::MakeTrackMeta` 同法）。
- 启动钩子：`initquit`（`initquit.h`，`on_init` 在主窗口创建后触发）。
- 右键菜单：`contextmenu.h` 的 `contextmenu_item_simple`，`context_get_display` 可自定义显示文本。
- 快捷键：`shortcut_actions.h` 已废弃；采用组件自装 `NSEvent` 本地监听。

## 目标

- Cmd+F 弹出/聚焦悬浮搜索框；Esc 或失焦关闭。
- 搜索**当前活动播放列表**，即时过滤，结果列表上下键选择，回车在列表中聚焦+选中+滚动可见（不自动播放）。
- 匹配 title/artist/album 三字段，大小写不敏感的子序列（模糊）匹配；中文支持全拼与首字母，含多音字。
- 歌单右键菜单提供入口，显示名标注键位 `(⌘F)`。
- core 匹配逻辑纯 C++、gtest 覆盖，含多音字用例。

## 非目标

- 键位不做偏好可配，硬编码 Cmd+F（YAGNI，后续可加）。
- 回车不播放（仅定位）。
- 不跨播放列表、不搜媒体库。
- 多音字表覆盖常用集，不追求全量字典。

## 分层结构

沿用项目 core / platform / ui 三层解耦。

### core（纯 C++，可单测）

`core/search/PlaylistSearchMatcher.{h,cpp}`

数据结构（跨字段通用）：

```cpp
namespace openlyrics {

// 一个源字符对应一个 cell，携带该字符的候选读音。
// 拉丁/数字：alternatives = { 该字符小写 }（单字符）。
// 汉字：alternatives = 全部拼音全拼读音（如 行 → {"hang","xing"}）。
struct SearchCell {
    std::vector<std::string> alternatives;  // 全拼候选，均小写；非空
    // 首字母集合由 alternatives 各元素首字符去重得到，可在构建时预存以省重复计算。
    std::vector<char> initials;
};

// 一个字段（title/artist/album）的 cell 序列。
using SearchField = std::vector<SearchCell>;

struct SearchRecord {
    SearchField title;
    SearchField artist;
    SearchField album;
    // 定位用的稳定标识由 platform 侧另存（metadb_handle_ptr），core 不感知。
};

// 匹配结果：命中记录在输入数组中的下标 + 分数（降序排序用）。
struct MatchHit {
    size_t index;
    int score;
};

// query 已由调用方小写化、去首尾空白。空 query 返回全部（保持原顺序，score 相等）。
std::vector<MatchHit> matchPlaylist(const std::vector<SearchRecord>& records,
                                    const std::string& query);
}  // namespace openlyrics
```

匹配算法（对每条记录、每个字段）：

- 两种子序列模式，取字段内最高分：
  - **全拼模式**：从左到右扫描 cell 序列，每个 cell 可跳过，或消费查询剩余前缀中等于该 cell 某个 `alternative` 的一段（拉丁 cell 即单字符）。全部查询字符被消费则命中。
  - **首字母模式**：每个 cell 可跳过，或消费一个等于该 cell 某个 `initial` 的查询字符。
- 打分优先级（分值区间设计时定，示意）：完全相等 > 前缀命中 > 连续子串命中 > 靠前起点 > 分散子序列；首字母模式命中给独立权重（一般低于全拼连续命中，高于全拼分散命中）。
- 跨字段（title/artist/album）取最大分；title 命中相对 artist/album 轻微加权。
- 未命中（任一模式均无法消费完整 query）该记录不入结果。
- 结果按分数降序、同分按原下标升序稳定排序。

core 不调用任何 Core Foundation，只吃已构建好的 `SearchRecord`。

### platform（Objective-C++ 适配）

`platform/PlaylistSearchBridge.{h,mm}`

- **拼音与 cell 构建**：
  - 对每个字段整串跑一次 `CFStringTransform`（`kCFStringTransformMandarinLatin` 再 `kCFStringTransformStripCombiningMarks`），得到按字对齐、空格分隔的默认读音（拉丁/数字原样透传）。
  - 逐源字符建 cell：拉丁/数字 → 单候选（小写自身）；汉字 → 若命中内置多音字表，用其读音集合（并入默认读音去重）；否则用该字默认读音单元素。
  - `initials` 由 alternatives 各首字符去重。
- **多音字表**：`platform/pinyin_polyphonic_table.inc`，由维护脚本生成的静态数据（汉字 UTF-32 码点 → 读音字符串列表），编入构建。覆盖常用多音字，可增量维护；其余汉字由 `CFStringTransform` 兜底。
- **快照**：遍历 `playlist_manager` 活动列表，`get_info_ref` 读 title/artist/album，构建 `std::vector<SearchRecord>`，并**并行保留每条的 `metadb_handle_ptr`**（下标对齐）。搜索框打开时建一次；大列表（数千条）一次性构建后驻留内存，按键仅在内存数组过滤。
- **定位**：给定命中下标，取对应 `metadb_handle_ptr`，调用 `activeplaylist_set_focus_by_handle` + `activeplaylist_set_selection_single`（清除其余选中）+ `activeplaylist_ensure_visible`。handle 已不在列表则静默忽略。

`platform/PlaylistSearchContextMenu.mm`

- 注册 `contextmenu_item_simple`（`FB2K_SERVICE_FACTORY`），单项。
- `context_get_display` / `get_item_name` 返回 `搜索定位歌曲  (⌘F)`。
- 执行回调忽略传入的 `metadb_handle_list`，`dispatch_async` 主线程调用 `PlaylistSearchController` 打开搜索框。
- `get_item_guid` 分配新 GUID；`get_item_default_path` 置根或组件分组。

### ui（Cocoa）

`ui/PlaylistSearchController.{h,mm}`（单例）

- 拥有 NSPanel（`NSTitledWindowMask` 可去标题的轻量面板），内含 NSTextField 搜索框 + NSScrollView 包裹的 NSTableView 结果列表。
- 打开：居中于 `NSApp.keyWindow`（无则 `mainWindow`）；调 `PlaylistSearchBridge` 建快照；成为 key window 并聚焦搜索框。
- 输入：`controlTextDidChange` 触发过滤——小写化 query → `matchPlaylist` → 刷新结果表（每行显示 `标题 — 艺术家`）。
- 键盘：↑↓ 移动结果选中（搜索框保持焦点，拦截方向键改选表行）；回车对当前选中结果执行定位并关闭；Esc 关闭。
- 关闭：`windowDidResignKey` 或 Esc 时隐藏面板，释放快照。
- 空态：无活动列表/空列表/无匹配时结果表空，回车无操作。

`ui/PlaylistSearchHotkey.mm`

- `initquit` 服务（`initquit_factory_t`）。`on_init` 中 `dispatch_async` 主线程装 `+[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:]`。
- 判定：`NSEventModifierFlagCommand` + 字符 `f`/`F`。命中则显示/聚焦 `PlaylistSearchController` 并返回 `nil` 吞掉事件（覆盖宿主对 Cmd+F 的处理）；否则原样返回事件透传。
- `on_quit` 移除监听。
- 监听句柄保存在文件内静态变量。

## 注册

在 `platform/component_entry.mm`（或新增入口文件）添加：`initquit` 工厂、`contextmenu_item` 工厂。UI element 与偏好页保持不变。

## 数据流

```
按 Cmd+F（NSEvent 监听）或右键菜单
  → PlaylistSearchController 打开
  → PlaylistSearchBridge 快照活动列表（含拼音 cell + handle）
  → 用户输入 → matchPlaylist 打分过滤 → 结果表
  → 回车 → 取选中 handle → set_focus_by_handle + set_selection_single + ensure_visible
  → 关闭
```

## 错误与边界

- 无活动列表 / 空列表：空态，回车无操作。
- 快照后列表被改：定位走 handle，抗重排；handle 失效则忽略。
- 无匹配：空结果，回车无操作。
- Cmd+F 与宿主冲突：监听命中时吞事件覆盖宿主。
- 面板失焦：自动关闭。

## 测试

- **core matcher（gtest）**：
  - 子序列跳过/消费、连续 vs 分散打分、前缀优先、跨字段取高分、title 加权。
  - 大小写不敏感；拉丁与汉字混排。
  - 多音字：`行` cell 候选 {hang,xing}、首字母 {h,x}；查询 `yinhang`/`yinxing`（银行）全拼命中，`yh`/`yx` 首字母命中。
  - 空 query 返回全部、保持原序。
- **platform 拼音归一化**：可纯化部分（去声调、取首字母、cell 构建对齐）尽量下沉为可测逻辑；`CFStringTransform` 调用为 platform 薄层，靠手动/集成验证。
- **手动真机**：Cmd+F 弹框、右键菜单入口与键位提示、即时过滤、回车定位不播放、Esc/失焦关闭、大列表性能。

## 打包

并入 `foo_openlyrics` 组件，新增上述文件，`platform/CMakeLists.txt` 纳入编译；多音字表 `.inc` 作为数据文件编入 platform 层。
