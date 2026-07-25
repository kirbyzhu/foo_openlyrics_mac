# foo_openlyrics_mac

foobar2000 for Mac 的歌词显示组件，功能参考 Windows 平台的 [foo_openlyrics](https://github.com/jacquesh/foo_openlyrics)，为 macOS 版从零实现。嵌入式 UI 面板 + 桌面歌词双形态，五级取词（内嵌标签 → 本地文件 → LrcLib → 网易云 → QQ 音乐），同步高亮平滑滚动，手动搜索与候选选择，offset 微调，内置歌词编辑器，偏好设置页。在线结果自动落盘缓存，断网优雅降级，失效源自动隔离。

> 计划一至八均已完成并合入 main，277 项核心单元测试全部通过（foobar2000 v2.25，Apple Silicon）。详见 [`docs/superpowers/STATUS.md`](docs/superpowers/STATUS.md)。

## 功能

### 歌词检索

- 五级取词管线，可配置顺序与启停。
  1. **内嵌标签** — 音频文件自身的歌词 metadata。
  2. **本地文件** — 同目录 `.lrc`/`.txt`，精确文件名 + 目录扫描归一化标题模糊匹配（UTF-8 感知，中文 bigram 相似度，兼容文件名/标签不一致）。
  3. **LrcLib** — 开放歌词接口，支持精准匹配与 `/api/search` 候选搜索。
  4. **网易云音乐** — EAPI 加密通道（AES-128-ECB + MD5），含逐字 YRC 行解析。
  5. **QQ 音乐** — 搜索 + base64 逐词歌词解码。
- 在线拉取结果自动落盘为 `<音频名>.lrc`，下次直接本地命中。不覆盖用户已存在文件。
- 失效源自动隔离（单源连续 5 次失败后临时禁用）。断网优雅降级。
- 切歌时 CancelToken 取消在飞请求，避免旧结果污染。

### 歌词显示

- 嵌入式 `ui_element_mac` 面板（布局 token `openlyrics`）。
- 整行高亮 + 60ms 插值平滑居中滚动，大幅跳转快速吸附。
- 增强型逐字 LRC 行内时标自动剥离（兼容网易云 YRC 格式）。
- 字体、颜色、对齐、行距均可在偏好设置中自定义。

### 桌面歌词

- 浮动透明 NSPanel 窗口，可拖拽定位。
- 前台自动隐藏 / 后台播放时自动浮现（可配置为常显）。
- 独立搜索管线与 60ms 同步 tick。
- 标题过长时自动换行（最多 2 行）。
- 右键菜单可退出或进入设置。
- 滚轮/方向键逐句手动打轴同步。

### 手动搜索与编辑

- 面板内 NSSearchField + NSPopover 候选列表（LrcLib/NetEase/QQ 多源搜索）。
- NSStepper offset 微调，实时生效并持久化写回 `.lrc`。
- NSTextView 编辑模式，LrcParser 回解析 + forceSave 覆写保存。

### 偏好设置

- `preferences_page_v4` 四 tab 设置页。
  - **数据源** — 拖拽排序启停各歌词源。
  - **显示** — 字体、颜色、对齐、行距。
  - **桌面歌词** — 启用/仅后台/字号/颜色/对齐/行距。
  - **高级** — HTTP 超时、默认 offset。

## 架构

三层解耦，核心可脱离 foobar2000 单元测试。

```
extensions/foo_openlyrics_mac/
├── core/       ← 可移植核心（纯 C++17，零平台依赖，命名空间 openlyrics）
│   ├── config/     AppConfig 配置模型 + JSON 序列化
│   ├── internal/   CancelToken 等内部工具
│   ├── matching/   Matcher（中文 bigram + 录音变体惩罚）
│   ├── model/      LyricData 歌词模型
│   ├── net/        UrlEncode / JsonField / Base64
│   ├── parser/     LrcParser / LrcSerializer（含 YRC 逐字行）
│   ├── pipeline/   SearchPipeline / SearchCoordinator
│   ├── ports/      HttpClient / FileSystem / TagIO / Clock / CryptoPort / ConfigPort
│   ├── sources/    TagSource / LocalFileSource / LrcLibProvider / NetEaseProvider / QQMusicProvider
│   ├── store/      LyricStore（落盘 + 查重）
│   └── sync/       SyncEngine（offset + 穿插无时标行）
├── platform/   ← SDK 胶水（Objective-C++）
│   ├── PlaybackBridge    播放回调 + PlaybackHub broker
│   ├── TagIOAdapter      metadb 读取
│   ├── FileSystemAdapter 文件操作
│   ├── HttpAdapter       NSURLSession 同步 HTTP
│   ├── CryptoAdapter     CommonCrypto AES/3DES/MD5
│   └── ConfigAdapter     NSUserDefaults 持久化
└── ui/         ← AppKit 面板与视图（Objective-C++）
    ├── LyricView                    歌词渲染视图
    ├── LyricPanelController         嵌入式面板控制器
    ├── DesktopLyricsController      桌面歌词浮窗控制器
    └── PreferencesViewController    偏好设置页
```

核心层经端口（`HttpClient`/`FileSystem`/`TagIO`/`Clock`/`CryptoPort`/`ConfigPort`）反向依赖平台，平台层提供实现。

## 构建

macOS，Command Line Tools + CMake，无需完整 Xcode。

### 前置

- macOS 11+（Big Sur 起），Command Line Tools（`xcode-select --install`）。
- CMake ≥ 3.20（`brew install cmake`）。
- **foobar2000 SDK** — 本仓库不包含（SDK 有其自身许可）。从 [foobar2000.org/SDK](https://www.foobar2000.org/SDK) 下载并解压到仓库根的 `SDK-2025-03-07/`（已在 `.gitignore` 中）。目录内应含 `foobar2000/`、`pfc/`、`libPPUI/`。

### 命令

```bash
# 核心单元测试（98 tests，不依赖 SDK 运行时）
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

# 构建组件 bundle 并安装到 foobar2000
cmake --build build --target foo_openlyrics
bash Scripts/install-component.sh
```

安装后**完全退出并重开** foobar2000。在 Preferences → Components 确认组件加载，然后在布局文本中加入 token `openlyrics`（macOS 版为纯文本布局，非右键菜单）。

## 路线图

计划一至七均已完成。后续迭代方向：

- **计划八（待定）** — 逐字高亮（word-level syllables）、翻译/双语歌词并排显示、Mini 模式（单行紧凑面板）。
- **计划九（待定）** — Spotify/Apple Music 等流媒体源、Last.fm scrobbling 回调、歌词社区贡献上传。

详见 `docs/superpowers/`：设计文档、各计划的分步实现文档与状态。

## 致谢

- **foobar2000 及其 SDK** — Peter Pawlowski（[foobar2000.org](https://www.foobar2000.org/)）。组件依托 foobar2000 for Mac 的 SDK 构建。
- **foo_openlyrics**（Windows）— jacquesh（[GitHub](https://github.com/jacquesh/foo_openlyrics)）。功能形态与交互设计的主要参考。
- **fb2k-components-mac-suite** — JendaT（[GitHub](https://github.com/JendaT/fb2k-components-mac-suite)）。macOS 组件的 CMake 构建结构参考。
- **LrcLib** — [lrclib.net](https://lrclib.net/)。开放歌词接口，桌面与内嵌面板的默认在线源。
- **网易云音乐歌词接口** — EAPI 加密通道（AES-128-ECB + MD5），参考社区公开的逆向成果，以 [NeteaseCloudMusicApi](https://github.com/Binaryify/NeteaseCloudMusicApi)（Binaryify）为代表。
- **QQ 音乐歌词接口** — 检索与逐词歌词 base64 解码流程，参考社区公开的逆向成果。

在线接口均归各自服务方所有，本组件仅在用户本机按需检索并缓存，不再分发。

## 许可

[MIT](LICENSE)。foobar2000 SDK 不含于本仓库，其使用受其自身许可约束。
