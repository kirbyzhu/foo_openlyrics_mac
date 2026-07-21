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

## P1 展示闭环（Task 3-4，待 P0 打通后展开为逐步任务）

P0（Task 1-2）验证了 SDK 编译、面板加载与打包路径后，再展开以下任务。其具体接口依赖 P0 中确认的 instantiate/线程/位置回调细节，故此处只列目标与判据。

**Task 3 PlaybackBridge（play_callback）**
交付。实现 `play_callback`（`SDK/play_callback.h`）订阅曲目切换/位置/暂停，抽取 `TrackMeta`（artist/title/album/path/length），推给面板 ViewModel。面板改为显示当前曲目标题与实时播放位置（秒）。判据。切歌与播放时面板文字实时更新，证明 SDK→核心的数据接线通。

**Task 4 展示闭环（TagIOAdapter/FileSystemAdapter + LyricView）**
交付。用 metadb/file_info 实现计划一的 `TagIO` 端口（读内嵌歌词标签）、用标准库/NSFileManager 实现 `FileSystem` 端口；接 `TagSource`+`LocalFileSource`+`SearchPipeline`(仅本地档)+`SyncEngine`；`LyricView` 做整行高亮 + 定时器驱动平滑滚动。判据。带内嵌或同目录 .lrc 的曲目，面板随播放高亮当前行并平滑居中滚动。

## 风险

- CLT 能否编译整套 SDK 尚未验证，Task 1 是关键闸门。若 BLOCKED，退回“装完整 Xcode 走官方 xcodeproj 路径”。
- `ui_element_mac` 无官方示例，注册细节（service_factory 具体模板、instantiate 的 arg/返回包装）需对照 `commonObjects-Apple.h`、`fooSampleMacPreferences.mm` 及已装 mac-suite 的行为核对。
- 面板 GUI 验证依赖人工在 foobar2000 中确认，无法自动化。
