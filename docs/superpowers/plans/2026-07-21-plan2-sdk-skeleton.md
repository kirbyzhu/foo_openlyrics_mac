# 计划二 SDK 骨架与展示闭环 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** 用 CLT + CMake 把 foobar2000 macOS SDK 编译起来，做出一个能被 foobar2000 v2.25 加载、可加入布局的最小 `ui_element_mac` 面板，再打通 `play_callback` 曲目/位置，最终从内嵌标签/本地文件取歌词在面板内整行高亮平滑滚动（Layer B SDK 胶水 + Layer C AppKit，消费计划一的可移植核心）。

**Architecture:** 三层中的 B、C 层。Layer A 核心（`openlyrics_core`，计划一已合入 main）保持不变，通过端口 `TagIO`/`FileSystem`/`HttpClient`/`Clock` 被 B 层实现注入。产物是 `foo_openlyrics.component` bundle，装到 `~/Library/foobar2000-v2/user-components/`。

**Tech Stack:** Objective-C++（AppKit，程序化建 UI，不用 xib 以免依赖 Xcode 的 ibtool）、C++17、CMake ≥ 3.20、Command Line Tools 的 clang、`codesign` ad-hoc 签名。构建路径为 CLT + CMake bundle（不装完整 Xcode），属未经验证路径，故 Task 1/2 为构建 bring-up 探针，允许迭代。

## Global Constraints（已核实的地面真相，逐条 verbatim）

- SDK 位于仓库内 `SDK-2025-03-07/`（已下载解压，gitignored），含 `foobar2000/{SDK,helpers,helpers-mac,foobar2000_component_client,foo_sample,shared}`、`pfc/`、`libPPUI/`。各库带独立 `.xcodeproj`，其 `project.pbxproj` 的 “in Sources” 列表即 mac 权威编译源集（含内部 `#ifdef _WIN32` 兜空文件，可照单编译）。
- 目标 foobar2000 为已装 `/Applications/foobar2000.app` v2.25.10，universal（x86_64+arm64）。组件至少构建 arm64；universal 更好但非必须。
- 组件入口：`foobar2000/foobar2000_component_client/component_client.cpp` 提供 mac 入口符号 `foobar2000_get_interface`（`__attribute__((visibility("default")))`），必须编入组件。
- 版本声明：`DECLARE_COMPONENT_VERSION(NAME,VERSION,ABOUT)`（`SDK/componentversion.h`），每组件恰一处。可选 `VALIDATE_COMPONENT_FILENAME("foo_openlyrics.dll")`（跨平台文件名校验，字面用 .dll 后缀，SDK 约定如此）。
- mac 类名后缀：每组件必须提供自己的 `foobar2000-mac-class-suffix.h`，单行 `#define FOOBAR2000_MAC_CLASS_SUFFIX _foo_openlyrics`，供 helpers-mac 的 `FB2K_OBJC_CLASS()` 防止 Obj-C 类名跨组件冲突。
- 面板接口 `ui_element_mac`（`SDK/ui_element_mac.h`）：
  ```cpp
  class ui_element_mac : public service_base {
      FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(ui_element_mac);
  public:
      virtual service_ptr instantiate(service_ptr arg) = 0; // arg=包装的 NSDictionary；返回包装的 NSViewController
      virtual bool match_name(const char* name) = 0;
      virtual fb2k::stringRef get_name() = 0;
      virtual GUID get_guid() = 0;
  };
  ```
  `instantiate` 返回用 `wrapNSObject`（`SDK/commonObjects-Apple.h/.mm`）包装的 `NSViewController*`。服务用 `service_factory` 静态注册。
- include 根：`<SDK/...>` → 加 `SDK-2025-03-07/foobar2000/`；`<pfc/...>`、`<libPPUI/...>` → 加 `SDK-2025-03-07/`。组件源惯例 `#include "stdafx.h"`（可参照 foo_sample/stdafx.h 自建一个）。
- bundle Info.plist 关键键（照已装 foo_jl_simplaylist）：`CFBundlePackageType=BNDL`、`CFBundleExecutable=foo_openlyrics`、`CFBundleIdentifier=com.foobar2000.foo-openlyrics`、`CFBundleName=foo_openlyrics`、`CFBundleSupportedPlatforms=[MacOSX]`、`LSMinimumSystemVersion=11.0`、`CFBundleShortVersionString`/`CFBundleVersion`。bundle 名 `foo_openlyrics.component`，可执行文件名 `foo_openlyrics`（无扩展名，置于 `Contents/MacOS/`）。
- 需链接的系统框架（至少）：`AppKit`、`Foundation`、`CoreFoundation`、`Cocoa`；音频/格式相关按编译报错增补（如 `AudioToolbox`、`Security`、`SystemConfiguration`）。
- 核心库 `openlyrics_core` 已在 main：源在 `extensions/foo_openlyrics_mac/core/`，纯 C++17 无平台依赖，B 层直接链接复用，勿改其纯净性。
- 提交信息简体中文动宾式。UI/SDK 层无法单元测试，验证靠构建产物加载与人工在 foobar2000 中确认。

---

### Task 1: SDK 编译 bring-up（构建探针）

**性质：** 这是未经验证路径的探针，预期需多轮编译修错（缺框架、include 根、Windows 专属符号、PCH、Obj-C++ 处理）。目标只求“SDK 及 pfc 能用 CLT clang 编译成可链接的静态库/对象集”，不含任何业务逻辑。

**Files:**
- Create: `extensions/foo_openlyrics_mac/platform/CMakeLists.txt`（或在根 CMake 增设 SDK 库目标，任选其一并说明）
- Create: `extensions/foo_openlyrics_mac/platform/stdafx.h`（组件预编译头，参照 `SDK-2025-03-07/foobar2000/foo_sample/stdafx.h`）
- 使用（只读，不改）：`SDK-2025-03-07/` 全部源

**Interfaces:**
- Consumes: 无（纯构建）
- Produces: CMake 目标 `fb2k_sdk`（静态库或 OBJECT 库），聚合 pfc + foobar2000/SDK + foobar2000/helpers + foobar2000/helpers-mac + component_client 的 mac 源，暴露正确 include 目录（`SDK-2025-03-07/` 与 `SDK-2025-03-07/foobar2000/`）。后续 Task 依赖此目标链接。

- [ ] **Step 1: 提取各库 mac 源清单**

从四个 pbxproj 提取 “in Sources” 文件名，作为编译源白名单（避免误编 Windows 专属源）：
```bash
cd SDK-2025-03-07
for p in pfc/pfc.xcodeproj foobar2000/SDK/foobar2000_SDK.xcodeproj \
         foobar2000/helpers/foobar2000_SDK_helpers.xcodeproj; do
  echo "== $p =="
  grep -oE '[A-Za-z0-9_./+-]+\.(cpp|mm|c|m) in Sources' "$p/project.pbxproj" | sort -u
done
```
helpers-mac 无独立 xcodeproj，其 `.mm/.m` 全部编入（`commonObjects-Apple.mm` 在 `foobar2000/SDK/`，wrapNSObject 所在，务必包含）。component_client.cpp 必编。

- [ ] **Step 2: 写 CMake SDK 目标**

新增一个 CMake 目标 `fb2k_sdk`，`target_include_directories` 加 `SDK-2025-03-07/` 与 `SDK-2025-03-07/foobar2000/`，源列表用 Step 1 清单（可先用 `file(GLOB ...)` 每个库目录的 `*.cpp *.mm *.m`，再排除 Step 5 中报错的 Windows 专属文件）。编译选项 `-std=c++17 -fobjc-arc`（Obj-C++ 文件），定义按需 `-DUNICODE` 等。链接框架 AppKit/Foundation/CoreFoundation/Cocoa。

- [ ] **Step 3: 写最小 stdafx.h**

参照 `SDK-2025-03-07/foobar2000/foo_sample/stdafx.h`，包含 `<SDK/foobar2000.h>` 等 SDK 伞头，供后续组件源引用。

- [ ] **Step 4: 首次配置构建**

```bash
/opt/homebrew/bin/cmake -S . -B build && /opt/homebrew/bin/cmake --build build --target fb2k_sdk 2>&1 | tail -40
```

- [ ] **Step 5: 迭代修错至 `fb2k_sdk` 编译通过**

按报错逐项处理：缺 include 根→补 include dir；未定义 Windows 符号→从源列表剔除该 Windows 专属 .cpp（或确认其 `_WIN32` 守卫）；Obj-C++ 报错→确认 `.mm/.m` 以 Obj-C++/Obj-C 编译并开 ARC；缺框架→`target_link_libraries` 增补。每次只改最小集合，记录最终排除清单与新增框架。done：`cmake --build build --target fb2k_sdk` 零错误。

- [ ] **Step 6: 提交**

```bash
git add extensions/foo_openlyrics_mac/platform/ CMakeLists.txt
git commit -m "用 CLT+CMake 编译 foobar2000 SDK 静态库"
```

**交付判据：** `fb2k_sdk` 目标在 CLT clang 下编译通过，暴露正确 include 与框架依赖，供 Task 2 链接。若多轮后仍卡在 SDK 源无法用 CLT 编译（如强依赖 Xcode 专有构建设置），报 BLOCKED 并列出具体阻塞点，供决定是否改装完整 Xcode。

---

### Task 2: 最小可加载 ui_element_mac 面板 + 打包安装

**Files:**
- Create: `extensions/foo_openlyrics_mac/platform/foobar2000-mac-class-suffix.h`（单行后缀宏）
- Create: `extensions/foo_openlyrics_mac/platform/component_entry.mm`（`DECLARE_COMPONENT_VERSION` + `ui_element_mac` 实现 + service_factory 注册）
- Create: `extensions/foo_openlyrics_mac/ui/LyricPanelController.mm` / `.h`（最小 NSViewController，loadView 里放一个可见 NSView + 居中 NSTextField 占位）
- Create: `extensions/foo_openlyrics_mac/platform/Info.plist.in`（CMake 配置模板）
- Create: `Scripts/install-component.sh`（拷 bundle 到 user-components）
- Modify: `CMakeLists.txt`（bundle 目标 + 打包 + codesign）

**Interfaces:**
- Consumes: `fb2k_sdk`（Task 1）
- Produces: `foo_openlyrics.component` bundle；面板类经 `FB2K_OBJC_CLASS` 后缀命名；ui_element_mac 服务的 `match_name` 认 `"openlyrics"`，`get_name` 返回 "OpenLyrics"，`get_guid` 用固定新 GUID。

- [ ] **Step 1: 类后缀头**

`foobar2000-mac-class-suffix.h`：
```cpp
#define FOOBAR2000_MAC_CLASS_SUFFIX _foo_openlyrics
```

- [ ] **Step 2: 最小面板 NSViewController**

`LyricPanelController.mm`：一个 `NSViewController` 子类（用 `FB2K_OBJC_CLASS(LyricPanelController)` 命名），`loadView` 构造带背景色的 `NSView` 与居中 `NSTextField` 显示 "OpenLyrics"。纯程序化，不用 xib。

- [ ] **Step 3: ui_element_mac 实现 + 注册**

`component_entry.mm`：`DECLARE_COMPONENT_VERSION("OpenLyrics","0.1.0","macOS lyrics panel")`；实现 `ui_element_mac`：`instantiate` 里 `[[LyricPanelController alloc] init]` 并 `return wrapNSObject(vc)`；`match_name` 认 "openlyrics"；`get_name`/`get_guid` 按上。用 `service_factory_single_t<...>` 注册。参照 `foo_sample/Mac/fooSampleMacPreferences.mm` 的 wrapNSObject 与服务注册写法。

- [ ] **Step 4: bundle 目标与 Info.plist**

CMake 加 `foo_openlyrics` MODULE bundle 目标（`MACOSX_BUNDLE` 或手工组 `.component` 目录结构），链接 `fb2k_sdk` + 我们的 `.mm` + `openlyrics_core`。用 `Info.plist.in` 配置上述关键键，`MACOSX_BUNDLE_BUNDLE_NAME` 等。产物目录 `Contents/{MacOS/foo_openlyrics, Info.plist}`，bundle 后缀 `.component`。

- [ ] **Step 5: ad-hoc 签名与安装脚本**

`codesign --force --deep --sign - foo_openlyrics.component`；`Scripts/install-component.sh` 把 bundle 拷到 `~/Library/foobar2000-v2/user-components/foo_openlyrics/`。

- [ ] **Step 6: 构建、安装、人工验证**

```bash
/opt/homebrew/bin/cmake --build build --target foo_openlyrics
bash Scripts/install-component.sh
open -a foobar2000
```
人工确认：foobar2000 组件列表出现 OpenLyrics；布局编辑模式下 Add UI Element 能看到并加入该面板；面板显示 "OpenLyrics" 占位文字，无崩溃。（此步需人眼确认 GUI，subagent 完成构建/安装后以 DONE_WITH_CONCERNS 交回，由人验证。）

- [ ] **Step 7: 提交**

```bash
git add extensions/foo_openlyrics_mac/ Scripts/ CMakeLists.txt
git commit -m "实现最小可加载 ui_element_mac 面板并打包安装"
```

**交付判据：** bundle 被 foobar2000 v2.25 加载，面板可加入布局并显示占位文字。这打通了 Layer B/C 的最小骨架。

---

## P1 展示闭环（Task 3-4）

P0（Task 1-2）已验证 SDK 编译、面板加载、打包安装与文本布局 token（`openlyrics`）解析。以下两任务打通 SDK→核心→面板的数据流并做出真实歌词展示。SDK 调用的确切签名由实现者对照下列已命名头文件确认，不臆造。

已核实的地面真相（供 Task 3-4）：
- `play_callback`（`SDK/play_callback.h`）全部回调在主线程。关键方法 `on_playback_new_track(metadb_handle_ptr)`、`on_playback_time(double p_time)`（每秒）、`on_playback_stop`、`on_playback_pause(bool)`、`on_playback_seek(double)`。静态注册用 `play_callback_static`（见同头文件下半部），或用 `play_callback_manager::get()->register_callback(...)`。impl 辅助基类 `play_callback_impl_base`。
- 精细位置（平滑滚动需 ~60ms）不靠 `on_playback_time`，改在面板层用 NSTimer/CVDisplayLink 轮询 `playback_control::get()->playback_get_position()`（`SDK/playback_control.h` 确认签名）。
- 曲目元数据从 `metadb_handle_ptr` 取：`playable_location`/`get_path()` 得路径，`get_info` 或 `titleformat` 得 artist/title/album/length（实现者对照 `SDK/metadb_handle.h`、`SDK/titleformat.h` 确认）。
- 面板实例由宿主创建，可能 0 或多个。需一个单例 broker（`PlaybackHub`）承接 play_callback 事件并转发给活跃面板；面板在出现/销毁时向 broker 注册/注销（弱引用），更新一律回主线程。

### Task 3: PlaybackBridge —— 打通 SDK→面板数据流

**Files:**
- Create: `extensions/foo_openlyrics_mac/platform/PlaybackBridge.h/.mm`（`play_callback` 实现 + `PlaybackHub` 单例 broker）
- Modify: `extensions/foo_openlyrics_mac/ui/LyricPanelController.mm`（改为显示当前曲目标题 + 实时位置，向 PlaybackHub 注册/注销）
- Modify: `CMakeLists.txt`（新增源）

**Interfaces:**
- Consumes: `fb2k_sdk`、Task 2 的面板；计划一 `TrackMeta`（`core/model/TrackMeta.h`）
- Produces: `PlaybackHub` 单例，提供 `currentTrack()->TrackMeta`、`positionMs()`，以及面板注册接口 `addObserver:/removeObserver:`；`play_callback` 把 `on_playback_new_track` 抽成 `TrackMeta` 存入 hub 并通知观察者。

- [ ] Step 1: 实现 `play_callback`（用 `play_callback_impl_base` 或 `play_callback_static`），订阅 new_track/time/stop/pause/seek。对照 `SDK/play_callback.h` 确认注册方式。
- [ ] Step 2: `PlaybackHub` 单例：持有当前 `TrackMeta` 与位置；`on_playback_new_track` 里用 titleformat 或 file_info 填 `TrackMeta`（artist/title/album/path/length），主线程通知观察者。
- [ ] Step 3: 面板 `LyricPanelController` viewWillAppear 向 hub 注册、dealloc 注销；显示 “<title> — <mm:ss>”，用 NSTimer(0.25s) 轮询 `playback_control::get()->playback_get_position()` 刷新位置文字。
- [ ] Step 4: 构建 + 签名 + 安装（复用 Scripts/install-component.sh）。core_tests 不受影响。
- [ ] Step 5: 提交（中文动宾）。
- [ ] Step 6: 人工验证：播放一首歌，面板显示曲目标题并每秒/每 0.25s 刷新播放位置；切歌时标题更新；停止时清空。（subagent 完成构建安装后 DONE_WITH_CONCERNS 交回人验证。）

**判据：** 面板随播放实时显示曲目标题与位置，证明 SDK→核心→面板数据流通。

### Task 4: 展示闭环 —— 适配器 + TagSource/LocalFileSource + LyricView

**Files:**
- Create: `extensions/foo_openlyrics_mac/platform/TagIOAdapter.mm`（实现计划一 `TagIO` 端口，读内嵌歌词标签）
- Create: `extensions/foo_openlyrics_mac/platform/FileSystemAdapter.mm`（实现 `FileSystem` 端口）
- Create: `extensions/foo_openlyrics_mac/core/sources/TagSource.*`、`LocalFileSource.*`、`core/pipeline/SearchPipeline.*`（计划一未建的核心件，纯 C++，可单测）
- Create: `extensions/foo_openlyrics_mac/ui/LyricView.mm/.h`（整行高亮 + 平滑滚动的 NSView）
- Modify: 面板改为宿主 LyricView，接 SyncEngine
- Modify: `CMakeLists.txt`、`tests/`（TagSource/LocalFileSource/SearchPipeline 单测）

**Interfaces:**
- Consumes: 计划一 `LrcParser`、`SyncEngine`、端口 `TagIO`/`FileSystem`；Task 3 `PlaybackHub`
- Produces: `TagSource`/`LocalFileSource`（实现 `LyricSource`）、`SearchPipeline`（本地档降级）；`LyricView` 消费 `SyncResult` 渲染。

- [ ] Step 1: 纯 C++ 侧 TDD（可单测）：`TagSource`（注入 TagIO 读内嵌）、`LocalFileSource`（注入 FileSystem 按 basename 找同目录 .lrc/.txt）、`SearchPipeline`（按序 Tag→本地，命中短路），用假端口写测试。
- [ ] Step 2: 平台适配器：`TagIOAdapter` 用 metadb/file_info 读常见歌词标签（LYRICS/UNSYNCEDLYRICS，对照 `SDK/file_info.h`）；`FileSystemAdapter` 用标准库/NSFileManager。
- [ ] Step 3: `LyricView`（NSView）：给定 `LyricData` 与当前 `SyncResult`，整行高亮当前行、平滑居中滚动（NSTimer ~60ms 插值）；字体/颜色先硬编码默认。
- [ ] Step 4: 面板接线：曲目切换（PlaybackHub 通知）→ 后台 SearchPipeline 取歌词 → 主线程交 LyricView；面板 timer 轮询位置 → SyncEngine.locate → LyricView 更新。
- [ ] Step 5: 构建 + 签名 + 安装；纯 C++ 单测全绿。
- [ ] Step 6: 提交（中文动宾）。
- [ ] Step 7: 人工验证：播放带内嵌歌词或同目录 .lrc 的曲目，面板随播放高亮当前行并平滑居中滚动；无歌词时显示占位。

**判据：** 带内嵌/本地歌词的曲目在面板内同步高亮滚动，计划二展示闭环完成。

## 风险

- CLT 能否编译整套 SDK 尚未验证，Task 1 是关键闸门。若 BLOCKED，退回“装完整 Xcode 走官方 xcodeproj 路径”。
- `ui_element_mac` 无官方示例，注册细节（service_factory 具体模板、instantiate 的 arg/返回包装）需对照 `commonObjects-Apple.h`、`fooSampleMacPreferences.mm` 及已装 mac-suite 的行为核对。
- 面板 GUI 验证依赖人工在 foobar2000 中确认，无法自动化。
