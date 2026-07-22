# 计划五 手动搜索 + 时轴 offset 微调 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** 当五级管线全部未命中时，用户可在面板内手动输入关键词搜索 LrcLib 候选歌词列表并点选应用；同时支持面板内实时微调 offset 并持久化回 .lrc 文件。

**Architecture:** LrcLibProvider 新增 `search()` 方法返回候选列表。面板新增搜索框（NSSearchField）+ 结果弹出面板（NSPopover 内嵌 NSTableView）。offset 微调用 NSStepper + 标签，每次步进 100ms，实时传 extraOffsetMs 给 SyncEngine::locate。确认后写回 `[offset:]` 到 .lrc 文件（覆写 sourceText 中的 offset 标签行）。

**Tech Stack:** 纯 C++17（core）、Objective-C++（platform/UI）、CMake、GoogleTest。

## 已核实地面真相

### LrcLib search API
- `GET https://lrclib.net/api/search?q=<urlencoded>` 返回 JSON 数组
- 每条结果含：`id`（数字）、`trackName`、`artistName`、`albumName`、`duration`（秒）
- 拿 `id` 后可通过既有 `fetch()` 的 `/api/get` 端点取完整歌词（LrcLibProvider 需支持按 id 取词）

### Offset 机制（既有代码）
- `LyricData::offsetMs`：LRC `[offset:]` 标签解析值，毫秒
- `SyncEngine::locate(data, positionMs, extraOffsetMs=0)`：runtime offset 叠加在 data.offsetMs 之上
- `LrcSerializer::serialize(data)`：当 data.offsetMs != 0 时写出 `[offset:<ms>]` 标签
- `LyricStore::save()` 写 `data.sourceText` 到 `<音频名>.lrc`，已存在文件不覆盖
- 当前 `extraOffsetMs` 恒为 0（LyricPanelController::tickSync 未传第三个参数）

### 落盘行为（既有）
- `LyricStore::save()` 仅在目标文件不存在时才写（防覆盖用户手工编辑的 .lrc）
- offset 调整后需覆写已存在的 .lrc——需要新增 `force` 参数或独立的 update 方法

## 设计决策

- **LrcLibProvider 新增 `search()` 方法**：签名 `bool search(const std::string& query, std::vector<SearchResult>& out)`，返回候选数组。SearchResult 为纯数据结构（id, trackName, artistName, albumName, duration）。
- **LrcLibProvider 新增 `fetchById()` 方法**：签名 `bool fetchById(int id, LyricData& out)`，拿 search 结果中的 id 去 `/api/get?id=` 取完整歌词并解析。
- **搜索 UI 实现为 NSPopover**：NSSearchField 输入关键词回车后弹出 popover，内嵌单列 NSTableView 显示"曲名 — 艺术家 — 专辑"。点选行后关闭 popover，调用 fetchById 取词。
- **Offset UI 放在状态行右侧**：两个 NSButton（-/+）和一个标签显示当前 offset 值（如 "+0.00s"）。步进 100ms，值传递为 `extraOffsetMs` 到 SyncEngine::locate，实现即时预览。
- **Offset 确认后覆写 .lrc**：点击"应用"或失焦时，若 offset 有变更且 source 非 tag（即歌词来自本地文件或在线），解析 sourceText 中的 `[offset:]` 行并更新/追加，然后强制写回 .lrc 文件。LyricStore 新增 `forceSave()` 方法跳过存在性检查。
- **只在有歌词时显示搜索框和 offset 控件**：未找到歌词时隐藏，避免界面杂乱。

## Task 1: LrcLibProvider 扩展 search + fetchById

**文件：**
- 新增 `core/model/SearchResult.h`：定义 `SearchResult` 结构体
- 修改 `core/sources/LrcLibProvider.h/.cpp`：新增 `search()` 和 `fetchById()` 方法

**search() 实现：**
1. GET `https://lrclib.net/api/search?q=<urlEncodeComponent(query)>`
2. status != 200 → 返回 false
3. 解析 JSON 数组，逐元素提取 id/trackName/artistName/albumName/duration
4. 填入 `vector<SearchResult>& out`，至少一条结果时返回 true

**fetchById() 实现：**
1. GET `https://lrclib.net/api/get?id=<id>`
2. status != 200 → 返回 false
3. 与 fetch() 相同的 JSON 解析逻辑（syncedLyrics/plainLyrics/instrumental）
4. 命中填 out 返回 true

**测试（test_lrclib_provider.cpp 扩展）：**
- SearchHit：search 返回非空数组
- SearchEmptyQuery：空查询返回 false
- SearchHttpError：非 200 返回 false
- FetchByIdHit：用 search 结果的 id 调 fetchById 能取到歌词
- FetchByIdInvalidId：无效 id 返回 false

## Task 2: 面板搜索 UI

**文件：** 修改 `ui/LyricPanelController.mm`

**实现：**
1. 新增 `NSSearchField` 属性，放在 statusLabel 上方（仅在有歌词或首次未命中时显示）
2. NSSearchField delegate：用户按 Enter 时触发搜索
3. 后台队列调用 `LrcLibProvider::search()`，结果 dispatch 回主线程
4. 弹出 NSPopover，内含 NSTableView（单列，NSAttributedString 显示"曲名\n艺术家 · 专辑"）
5. NSTableView 点击行 → 关闭 popover → 后台调用 `fetchById()` → 主线程更新展示 + 落盘
6. 搜索进行中显示"搜索中…"，无结果显示"未找到匹配项"

**布局调整：**
```
┌─────────────────────────┐
│  [🔍 搜索歌词…]         │  ← NSSearchField（未命中或有歌词时显示）
│  曲名 · 检索歌词中…     │  ← statusLabel（现有）
│  ─────────────────────  │
│  歌词行 1               │
│  歌词行 2               │  ← LyricView（现有）
│  ...                    │
│  ─────────────────────  │
│  [-0.10s]  [应用]       │  ← offset 控件（Task 3）
└─────────────────────────┘
```

## Task 3: Offset 微调 UI

**文件：** 修改 `ui/LyricPanelController.mm`

**实现：**
1. 新增 offset 控件组：`NSStepper`（范围 -30.0~+30.0s，步进 0.1s）+ `NSTextField` 标签显示当前值
2. 控件组放在 LyricView 下方，仅在有歌词时显示
3. NSStepper 值变化 → 更新标签文本 → 更新 `_currentExtraOffsetMs` → 下一次 tickSync 自动生效
4. tickSync 改为 `SyncEngine::locate(_currentLyricData, posMs, _currentExtraOffsetMs)`
5. 新增 NSButton "应用"：点击后将 offset 值写入 .lrc 文件并重置 `_currentExtraOffsetMs` 为 0（offset 已固化到 data.offsetMs）

**NSStepper 配置：**
- minValue = -30.0, maxValue = 30.0, increment = 0.1
- valueWraps = NO, autorepeat = YES

## Task 4: Offset 持久化

**文件：**
- 修改 `core/store/LyricStore.h/.cpp`：新增 `forceSave()` 方法
- 修改 `ui/LyricPanelController.mm`：offset 应用的写文件逻辑

**forceSave() 实现：**
1. 与 save() 相同的路径计算逻辑
2. 不检查目标文件是否已存在——直接覆写
3. 写 data.sourceText 到文件

**offset 写入逻辑（LyricPanelController）：**
1. 用户点"应用" → 读取当前 sourceText
2. 若 sourceText 已有 `[offset:...]` 行 → 替换为新值
3. 若 sourceText 无 offset 行 → 在最后一个 id 标签后插入 `[offset:<ms>]`
4. 更新 `_currentLyricData.offsetMs` 和 `_currentLyricData.sourceText`
5. 调用 `LyricStore::forceSave()` 覆写 .lrc
6. 重置 `_currentExtraOffsetMs = 0`，更新 UI 标签为 "+0.00s"

## 任务依赖

```
Task 1 (search + fetchById) ──┐
                               ├──> Task 2 (搜索 UI)
                               │
Task 3 (offset UI) ───────────┼──> Task 4 (持久化)
                               │
                               └──> 人工验证
```

## 全局约束（承自计划一至四）

- 纯 C++ 核心仅 std-lib，命名空间 `openlyrics`，禁 SDK/AppKit/ObjC
- 平台层 Obj-C++ ARC
- 不破坏既有 115 项测试与 fb2k_sdk/foo_openlyrics 目标
- 提交信息简体中文动宾式
- UI/网络层人工验证
