# foo_openlyrics_mac

foobar2000 for Mac 的歌词显示组件，功能参考 Windows 平台的 [foo_openlyrics](https://github.com/jacquesh/foo_openlyrics)，为 macOS 版重新实现。以嵌入式 UI 面板形式集成到 foobar2000 主界面，随播放同步高亮滚动歌词，支持内嵌标签、本地文件与在线（LrcLib）三级取词并自动落盘缓存。

> 状态：核心功能已可用并经真机验证（foobar2000 v2.25，Apple Silicon）。中文源、手动搜索、内置编辑器等增强项在路线图中。详见 [`docs/superpowers/STATUS.md`](docs/superpowers/STATUS.md)。

## 功能

- 嵌入式 `ui_element_mac` 面板，加入 macOS 文本布局（token `openlyrics`）。
- 三级取词：内嵌歌词标签 → 同目录本地 `.lrc`/`.txt` → 在线 LrcLib。
- 本地匹配：精确文件名 + 目录扫描的归一化标题模糊匹配（UTF-8 感知，兼容中英文与文件名/标签不一致）。
- 整行高亮 + 平滑居中滚动；大幅跳转快速吸附；增强型逐字 LRC 行内时标自动剥离。
- 在线拉取结果无损落盘为 `<音频名>.lrc`，下次直接本地命中；不覆盖用户已存在文件；断网优雅降级。

## 架构

三层解耦：

- `extensions/foo_openlyrics_mac/core/` —— 可移植核心，纯 C++17、零平台依赖，命名空间 `openlyrics`，命令行可单测（歌词模型、LRC 解析/序列化、同步引擎、歌词源与检索管线、URL 编码、JSON 提取、落盘、端口抽象）。
- `extensions/foo_openlyrics_mac/platform/` —— SDK 胶水，Objective-C++（播放回调桥接、metadb/文件/HTTP 端口实现、组件注册）。
- `extensions/foo_openlyrics_mac/ui/` —— AppKit 面板与歌词视图。

核心经端口 `HttpClient`/`FileSystem`/`TagIO`/`Clock` 反向依赖平台，平台层提供实现，因此业务逻辑可脱离 foobar2000 单元测试。

## 构建（macOS，Command Line Tools + CMake，无需完整 Xcode）

前置：
- macOS 11+（Big Sur 起），Command Line Tools（`xcode-select --install`）。
- CMake ≥ 3.20（`brew install cmake`）。
- **foobar2000 SDK**：本仓库不包含（SDK 有其自身许可，不予转发）。请从 [foobar2000.org/SDK](https://www.foobar2000.org/SDK) 下载并解压到仓库根的 `SDK-2025-03-07/`（该目录已在 `.gitignore` 中）。目录内应含 `foobar2000/`、`pfc/`、`libPPUI/`。

命令：

```bash
# 命令行核心单元测试（不依赖 SDK 中除编译外的运行时）
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

# 构建组件 bundle 并安装到 foobar2000
cmake --build build --target foo_openlyrics
bash Scripts/install-component.sh
```

安装后**完全退出并重开** foobar2000。在 Preferences → Components 确认组件加载，然后在布局文本中加入 token `openlyrics`（macOS 版为纯文本布局，非右键菜单）。

## 路线图

- 计划四：中文歌词源（NetEase / QQ 音乐），可插拔、可整源禁用。
- 计划五：手动搜索（LrcLib `/api/search`）与时轴 offset 微调。
- 计划六：内置歌词编辑器与偏好设置页。

详见 `docs/superpowers/`：设计文档、各计划的分步实现文档与状态。

## 致谢

- 功能参考 [foo_openlyrics](https://github.com/jacquesh/foo_openlyrics)（Windows）。
- 在线歌词来自 [LrcLib](https://lrclib.net/)。
- macOS 组件构建结构参考 [fb2k-components-mac-suite](https://github.com/JendaT/fb2k-components-mac-suite)。

## 许可

[MIT](LICENSE)。foobar2000 SDK 不含于本仓库，其使用受其自身许可约束。
