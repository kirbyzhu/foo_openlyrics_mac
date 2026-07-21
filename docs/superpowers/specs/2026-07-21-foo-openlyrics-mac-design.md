# foo_openlyrics_mac 设计文档

日期 2026-07-21
状态 已确认，待进入实现计划
目标平台 foobar2000 for Mac v2.x（最低系统 macOS 11 Big Sur）
交付目标 个人自用为主，构建流程从简，不强制 Apple 公证与 CI

## 1. 背景与目标

在 macOS 版 foobar2000 上以第三方组件形式实现歌词显示面板，功能参考 Windows 组件 [foo_openlyrics](https://github.com/jacquesh/foo_openlyrics)。

关键前提已联网核实。`foo_openlyrics` 是 Windows 专属组件，基于 Win32 与 Windows SDK 的面板系统编写，无法直接移植。macOS 版 foobar2000 为全新模块化 UI，第三方组件通过 SDK 的 `ui_element_mac` 接口注册为可停靠 UI 元素。因此本项目是一次移植与重写，而非重新编译。

核实来源
- foobar2000 SDK 与 macOS 组件开发，`ui_element_mac` 接口，Xcode 工程，SDK 版本 `SDK-2025-03-07`。https://www.foobar2000.org/SDK
- 现成开源骨架参考 `JendaT/fb2k-components-mac-suite`（Objective-C++，`extensions/foo_xx_mac/` 源码、`shared/` 公共代码、`Scripts/` 的 Ruby 工程生成与 `build.sh`，产物 `.fb2k-component` 安装到 `~/Library/foobar2000-v2/user-components/`）。https://github.com/JendaT/fb2k-components-mac-suite
- Mac 2.24 release notes，模块化 UI 与内置面板说明。https://www.foobar2000.org/releasenotes-mac-2.24

## 2. 功能范围（首版）

歌词来源，四类全部纳入
- 内嵌标签。从音频文件 tag（如 LYRICS / UNSYNCEDLYRICS / 内嵌 LRC）读取。
- 本地歌词文件。同目录或指定目录下的 .lrc / .txt，按文件名模板匹配。
- 在线 provider，LrcLib 优先。
- 多个中文 provider，NetEase、QQ 音乐。

交互与写回，全部纳入
- 自动保存拉取结果。在线拉到后自动落盘（写本地 .lrc 或写回 tag），命名与目录可配置。
- 手动搜索与选择。弹窗手动搜索、列出候选、人工选定并应用。
- 时轴偏移微调。实时 offset，可持久化到文件或标签。
- 内置编辑器。面板内编辑歌词文本与时标（工作量最大，排实现末位，接口先留好）。

展示精度
- 整行高亮 + 平滑滚动。标准 LRC，按行时标高亮当前行并平滑居中滚动。数据模型预留 word-level 字段但首版不填充。

## 3. 架构取舍

被否决备选。把歌词做成独立 AppKit 窗口、由组件后端驱动。SDK 已确认支持 `ui_element_mac` 嵌入式面板，独立窗口会脱离布局系统、体验割裂，故不采用。

最终方案。嵌入式面板，并把可移植逻辑与平台 UI 彻底分层，核心层零平台依赖、可脱离 foobar2000 单元测试。

## 4. 总体架构（三层解耦）

```
Layer C  AppKit UI 面板 (Objective-C++ / AppKit)      依赖 SDK + Cocoa
Layer B  SDK 胶水层     (Objective-C++)               依赖 foobar2000 SDK
Layer A  可移植核心     (纯 C++17，无 SDK / 无 AppKit)  零平台依赖，可单测
```

依赖方向自上而下。Layer A 通过抽象端口（HttpClient / FileSystem / TagIO / Clock）反向依赖平台，由 Layer B 提供端口实现。歌词源、解析、同步、存储逻辑因此可在命令行下脱离 foobar2000 测试。

## 5. 模块清单

### 5.1 Layer A 可移植核心（纯 C++17）

| 模块 | 职责 | 关键接口 |
|---|---|---|
| `LyricData` | 结构化歌词模型。元数据 + 行数组（每行 时标ms + 文本，预留 `syllables` word-level 字段，首版不填充）。标记 synced / unsynced | 值对象 |
| `LrcParser` / `LrcSerializer` | LRC 解析与回写。支持多时标行、`[offset:]`、ID 标签、纯文本回退、畸形输入容错 | `parse(text)->LyricData`，`serialize(LyricData)->text` |
| `LyricSource`（抽象） | 单一歌词源统一接口 | `search(TrackMeta)->candidates`，`fetch(candidate)->LyricData` |
| `TagSource` | 从内嵌标签取歌词 | 实现 `LyricSource` |
| `LocalFileSource` | 从 .lrc / .txt 目录 + 文件名模板取 | 实现 `LyricSource` |
| `LrcLibProvider` | LrcLib 在线源 | 实现 `LyricSource` |
| `NetEaseProvider` | 网易云音乐在线源 | 实现 `LyricSource` |
| `QQProvider` | QQ 音乐在线源 | 实现 `LyricSource` |
| `SourceRegistry` | 源注册表。优先级顺序、单源启停、失效隔离 | 注册 / 排序 / 启禁 |
| `SearchPipeline` | 按序查源、命中短路、候选聚合。注入 Http 与 IO | `resolve(TrackMeta)->LyricData` |
| `LyricStore` | 自动保存策略。写本地 .lrc 或写回 tag，目录 / 命名模板，覆盖策略 | `save(LyricData, TrackMeta)` |
| `SyncEngine` | 纯函数。位置ms + LyricData + offset → 当前行索引 + 滚动插值进度 | `locate(posMs)->{line, progress}` |
| ports/ | `HttpClient`、`FileSystem`、`TagIO`、`Clock` 抽象 | 依赖倒置边界 |

### 5.2 Layer B SDK 胶水（Objective-C++）

| 模块 | 职责 |
|---|---|
| `PlaybackBridge` | 实现 `play_callback`，映射曲目切换 / 位置 / 暂停；抽取 `TrackMeta`（artist / title / album / path / length） |
| `TagIOAdapter` | 用 SDK 的 metadb / file_info 实现 `TagIO`（读内嵌歌词、写回 tag） |
| `HttpAdapter` | 实现核心 `HttpClient` 端口（NSURLSession 或 SDK http_client） |
| `FileSystemAdapter` | 实现核心 `FileSystem` 端口 |
| `PreferencesPageProvider` | 注册配置页，承载 Layer C 的设置面板 |
| `ComponentEntry` | 组件版本声明与服务工厂注册 |

### 5.3 Layer C AppKit UI（Objective-C++ / AppKit）

| 模块 | 职责 |
|---|---|
| `OpenLyricsUIElement` | 实现 `ui_element_mac`，返回 NSView，注册为可加入布局的 UI 元素 |
| `LyricView`（NSView 子类） | 整行高亮 + 平滑滚动渲染。字体 / 字号 / 颜色 / 对齐 / 行距可配置。定时器驱动滚动插值 |
| `ViewModel` | 桥接 SyncEngine 输出与 Pipeline 状态（当前行 / 加载态 / 错误态 / 无歌词占位） |
| `SearchDialogController` | 手动搜索候选列表窗口 |
| `OffsetControl` | 面板内实时 offset 微调，可持久化 |
| `LyricEditorController` | 内置编辑器（排实现末位，接口先留好） |

## 6. 数据流

拉取管线
1. 曲目切换，`PlaybackBridge` 取 `TrackMeta`。
2. 后台队列跑 `SearchPipeline`，顺序 Tag → 本地文件 → LrcLib → NetEase → QQ，命中即短路。
3. 得 `LyricData`。若来自在线源，`LyricStore` 自动落盘。
4. 回主线程交 `ViewModel`。

播放同步
1. 主线程轻量定时器（约 60ms）读播放位置。
2. `SyncEngine.locate` 算当前行与插值进度。
3. `LyricView` 平滑滚动并高亮当前行。

## 7. 线程模型

网络与文件 IO 全在 GCD 后台队列执行。`SyncEngine` 计算极轻，随定时器在主线程运行。所有视图更新回主线程。每个在线源独立超时，互不阻塞。

## 8. 错误处理与降级

降级链 Tag → 本地 → 在线（按序）。单个在线源失败仅记日志并继续下一个，不中断管线。中文 provider 因加密 / 签名 / 限流失效时，该源在配置页自动标红并跳过，整体仍可用。无任何结果时展示占位并引导手动搜索。写回失败不影响展示。

## 9. 配置项

- 源顺序与单源开关
- 自动保存（开关 + 目标文件 / tag + 命名模板 + 目录）
- 字体、字号、颜色、对齐、行距
- 默认 offset
- 网络超时
- 日志级别

## 10. 目录结构（照搬 mac-suite 骨架）

```
foo_openlyrics_mac/
  extensions/foo_openlyrics_mac/
    core/     model/ parser/ sources/ pipeline/ store/ sync/ ports/   # Layer A 纯 C++
    platform/                                                          # Layer B obj-c++
    ui/                                                                # Layer C AppKit
    resources/
    component_client.cpp
  shared/                    # SDK helper，取自 suite
  Scripts/                   # generate_xcode_project.rb, build.sh
  tests/                     # 核心层命令行单测，脱离 foobar 运行
  SDK-2025-03-07/            # 外部下载放置，不入库（.gitignore）
  docs/
```

## 11. 测试策略

核心层独立测试目标（Catch2 或 GoogleTest），命令行运行，覆盖
- `LrcParser`，各类时标、offset、多时标行、畸形输入。
- `SyncEngine`，边界、负 offset、跨行、空歌词。
- `SearchPipeline`，短路命中、逐级降级、mock Http。
- `LyricStore`，命名模板、目录模板、覆盖策略。

Provider 用录制的样例响应做离线测试，不依赖真实网络。平台层用假的端口实现做集成测试。UI 手动验证，辅以少量快照。

## 12. 实现分期

| 阶段 | 内容 | 交付判据 |
|---|---|---|
| P0 骨架 | 工程生成、组件注册、空面板能加入布局、`PlaybackBridge` 打通曲目 / 位置 | 面板可在布局编辑器中加入并显示，能打印当前曲目与播放位置 |
| P1 展示闭环 | `LyricData` + `LrcParser` + `SyncEngine` + `LyricView`，从 Tag / 本地取，整行高亮滚动 | 带内嵌或本地 LRC 的曲目能同步高亮滚动 |
| P2 在线 + 保存 | `HttpAdapter` + `LrcLibProvider` + `SearchPipeline` + `LyricStore` 自动落盘 | 无本地歌词时能从 LrcLib 拉取并自动保存，下次命中本地 |
| P3 中文源 | NetEase / QQ provider，可插拔、离线样例测试 | 两个中文源可独立启停，失效不影响整体 |
| P4 交互 | 手动搜索对话框、offset 微调 | 可手动搜索选定并应用，可实时调 offset 并持久化 |
| P5 编辑器 | 内置编辑器 | 面板内可编辑文本与时标并保存 |
| P6 配置页 | 配置项完善 | 全部配置项可在偏好设置页调整并生效 |

## 13. 风险与待核实项

- SDK 具体接口名（`ui_element_mac`、`play_callback`、mac 上配置页对应类、metadb / file_info 写标签的确切用法）需在 P0 对照真实 SDK 头文件核对，不凭记忆写死。
- NetEase / QQ provider 依赖逆向所得的加密 / 签名接口，随服务端改动随时可能失效，属高维护成本项。框架以可插拔 + 失效隔离兜底。首版应保留其可整源禁用能力。
- 内嵌歌词标签的字段命名（LYRICS / UNSYNCEDLYRICS 及各封装格式差异）需在 P1 落地时以实际文件核对。
