# foo_openlyrics_mac 项目状态与后续工作

最后更新 2026-07-22。本文件持久记录进度与路线图，供任意新会话续接。账本 `.superpowers/sdd/progress.md` 为临时草稿（gitignored，可能丢失），以本文件与 git 历史为准。

## 一、项目概览

foobar2000 macOS 歌词显示组件，参考 Windows 的 foo_openlyrics。本地工作副本置于 `~/foo_openlyrics_mac`（git，工作在 `main`）。

三层解耦架构：
- Layer A 可移植核心 `core/`（纯 C++17，零 SDK/AppKit，命名空间 `openlyrics`，命令行可单测）。
- Layer B SDK 胶水 `platform/`（Objective-C++，foobar2000 SDK）。
- Layer C AppKit 面板 `ui/`（Objective-C++）。
- A 层经端口 `HttpClient/FileSystem/TagIO/Clock` 反向依赖平台，B 层提供实现。

## 二、构建 / 测试 / 安装（CLT + CMake，不需完整 Xcode）

前置：cmake（`/opt/homebrew/bin/cmake`，4.4.0）；SDK 已解压于 `SDK-2025-03-07/`（gitignored，来源 foobar2000.org/downloads/SDK-2025-03-07.7z）；目标 foobar2000 v2.25.10。

```bash
# 命令行核心单测（当前 98/98）
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
# 构建并安装组件到 foobar2000
cmake --build build --target foo_openlyrics && bash Scripts/install-component.sh
# 之后需完全退出并重开 foobar2000 才会加载新 binary
```

面板加入方式：macOS 是**纯文本布局**，无右键 Add UI Element。在布局文本中加入 token `openlyrics`（由 `ui_element_mac::match_name` 解析）。

## 三、已完成（均合入 main，人工端到端验证，98/98 测试）

### 计划一 可移植核心（`docs/superpowers/plans/2026-07-21-plan1-portable-core.md`）
`LyricData` 模型（含 `sourceText`）、`LrcParser`/`LrcSerializer`（畸形输入加固：超长数字防崩溃、算术防溢出、标签尾随空白、offset 容错、**增强型逐字 LRC 行内时标剥离**）、`SyncEngine`（offset 语义 + 穿插无时标行）、四端口抽象。

### 计划二 SDK 骨架与展示闭环（`docs/superpowers/plans/2026-07-21-plan2-sdk-skeleton.md`）
`fb2k_sdk`（CLT 编译 SDK）、`ui_element_mac` 面板打包安装、`PlaybackBridge`（play_callback + PlaybackHub broker，NSLock 线程安全）、`TagIOAdapter`/`FileSystemAdapter`、`LocalFileSource`（精确 + 目录扫描归一化标题模糊匹配，UTF-8 感知）、`SearchPipeline`、`LyricView`（整行高亮 + 60ms 插值平滑滚动）。含 4 项 post-merge 清理。

### 计划三 LrcLib 在线拉取与自动保存（`docs/superpowers/plans/2026-07-22-plan3-lrclib.md`）
`core/net/UrlEncode`（RFC3986）、`core/net/JsonField`（自写顶层 JSON 提取 + 反转义，无三方库）、`core/sources/LrcLibProvider`、`core/store/LyricStore`（写 `<音频名>.lrc`，落盘前查重不覆盖已存在文件）、`platform/HttpAdapter`（NSURLSession 同步 + 11s 硬超时 + 默认 UA，仅后台线程）。接线：Tag→本地→（未命中再）LrcLib→命中先落盘再显示。

### 计划四 中文源（NetEase / QQ 音乐）（`docs/superpowers/plans/2026-07-22-plan4-chinese-sources.md`）
`CryptoPort`（AES-128-CBC / 裸 RSA / 3DES-ECB / MD5）、`Base64`、`HttpClient::post`、`JsonField` 扩展（int/object 提取）、`NetEaseProvider`（weapi 双层 AES + 裸 RSA）、`QQMusicProvider`（搜索 + base64 解码）、`CryptoAdapter`（CommonCrypto + Security.framework + ASN.1 DER 手工构造）、`LyricPanelController` 五级管线（Tag→Local→LrcLib→NetEase→QQMusic）+ 失效隔离（单源连续 5 次失败禁用）。115/115 核心测试，5 个提交合入 main。

**待人工验证：** 计划四中文源命中与落盘，计划五搜索与 offset 微调，计划六编辑模式与配置页——均在 foobar2000 中操作确认。

**当前可用能力**：嵌入式面板、五级取词（Tag/Local/LrcLib/NetEase/QQMusic 可配置顺序启停）、手动搜索 LrcLib 候选列表、面板内 offset 实时微调并持久化、面板内编辑歌词文本/时标并保存、偏好设置页（数据源/显示/高级三 tab）、同步高亮平滑滚动、在线结果自动落盘缓存、断网优雅降级、失效源自动隔离。

### 计划五 手动搜索 + offset 微调（`docs/superpowers/plans/2026-07-22-plan5-manual-search-offset.md`）
`LrcLibProvider::search()/fetchById()`、面板 NSSearchField + NSPopover 搜索结果、NSStepper offset 微调 + extraOffsetMs 实时生效、`LyricStore::forceSave()` 覆写已存在 .lrc、offset 写回 sourceText。123/123 核心测试，2 个提交合入 main。

### 计划六 内置编辑器 + 配置页（`docs/superpowers/plans/2026-07-22-plan6-editor-config.md`）
`AppConfig` 纯 C++ 配置模型 + JSON 序列化、`ConfigAdapter`（NSUserDefaults 持久化）、`preferences_page_v4`（数据源/显示/高级三 tab，拖拽排序）、NSTextView 编辑模式（LrcParser 回解析 + forceSave）、`LyricView::applyDisplayConfig`（字体/颜色/对齐/行距）、HttpAdapter 全局可配超时、defaultOffsetMs 自动生效。134/134 核心测试，2 个提交合入 main。

### 计划七 桌面歌词（`docs/superpowers/plans/2026-07-23-plan7-desktop-lyrics.md`）
`DeskLyricsConfig` 配置模型 + JSON 序列化、`DesktopLyricsController`（NSPanel 浮动透明窗口 + 独立搜索管线 + 60ms 同步 tick + 前后台自动显隐）、偏好设置页"桌面歌词"tab（启用/仅后台/字号/颜色/对齐/行距）、`LyricView` 透明背景与 stopAnimation 支持。138/138 核心测试，4 个提交合入 main。

**待人工验证：** foobar2000 后台播放时桌面歌词窗口浮现、切回前台自动隐藏、拖拽移动、字号颜色配置即时生效。

## 四、后续路线图

计划一至七均已完成。后续迭代方向：
- **计划八（待定）**：逐字高亮（word-level syllables）、翻译/双语歌词并排显示、Mini 模式（单行紧凑面板）。
- **计划九（待定）**：Spotify/Apple Music 等流媒体源、Last.fm scrobbling 回调、歌词社区贡献上传。

## 五、遗留 Minor 待办（非阻塞，可顺手清）

- 计划三：`JsonField` 畸形孤立 `-`/孤立代理码点边界（LrcLib 不产生）；切歌不取消在途 HTTP 请求（仅弃结果）；非本地路径（流媒体）落盘目标无意义（best-effort，saved=no）。
- 计划二：`LyricView` 动画定时器空转不休眠；切歌瞬间旧歌词短暂残留（自纠正）；`NSBox.borderType` 弃用告警。
- 计划一：序列化精度上限厘秒；单行多时标不回写紧凑形式；`SyncEngine` offset 三值求和理论溢出。

## 六、已核实关键事实（勿重新臆造）

- `ui_element_mac`（`SDK/ui_element_mac.h`）面板返回 `wrapNSObject(NSViewController*)`；服务用 `FB2K_SERVICE_FACTORY` 注册。
- `play_callback_static` 订阅播放（**全部回调在主线程**）；`playback_control::get()->playback_get_position()` 取位置。
- `metadb_handle::get_info_ref()` 读 file_info（`get_info` 已废弃）；`get_path()` 返回 `file://` fb2k 路径，须 `filesystem::g_get_native_path` 转原生。
- 每组件唯一 `FOOBAR2000_MAC_CLASS_SUFFIX`（本项目 `_foo_openlyrics_mac`）；`DECLARE_COMPONENT_VERSION` 恰一处。
- LrcLib：`GET https://lrclib.net/api/get?artist_name=&track_name=&album_name=&duration=` → JSON（`syncedLyrics`/`plainLyrics`/`instrumental`），未找到 404，需 User-Agent。`/api/search?q=` 返回候选数组（计划五用）。

## 七、如何续接（新会话）

1. 新会话会自动加载项目记忆（含本文件指针）。若未加载，让我读 `docs/superpowers/STATUS.md`。
2. 直接说“着手计划四”（或五/六），我会：新建分支 → 用 writing-plans 写该计划的 bite-sized 任务文档 → 用 subagent-driven-development 逐任务实现+审查 → 整分支最终审查 → 合回 main。
3. 计划四开工前需先获取 NetEase/QQ 的接口样例（可让我联网探测，或你提供）。
4. 平台/UI/网络类改动无法命令行验证，需你在 foobar2000 里人工确认（我会给出具体步骤）。
