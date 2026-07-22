# 计划六 内置编辑器 + 配置页 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** 面板内可切换编辑模式直接修改歌词文本与时标并保存；提供 foobar2000 偏好设置页承载源顺序/开关、字体字号颜色对齐行距、默认 offset、超时等配置项。

**Architecture:** 新增 `core/config/AppConfig.h` 纯 C++ 配置模型，JSON 序列化。平台层 `ConfigAdapter` 用 NSUserDefaults 持久化。偏好设置页 NSViewController 通过 `preferences_page_v4` + `preferences_mac_common` 模板注册到 SDK。面板编辑模式用 NSTextView 覆层替换 LyricView，保存时 LrcParser 回解析。

**Tech Stack:** 纯 C++17（core）、Objective-C++（platform/UI）、CMake、GoogleTest。

## 已核实地面真相

### 偏好设置页注册（SDK）
- `preferences_page_v4` 虚接口：`get_name()`、`get_guid()`、`get_parent_guid()`、`instantiate()` 返回 `wrapNSObject(NSViewController*)`
- `preferences_mac_common<T>` 模板：继承 `preferences_page_v4`，`instantiate()` 返回 `[T new]`
- `preferences_page_factory_t<T>` 注册工厂
- `preferences_branch_factory` 创建分组节点
- 标准父 GUID：`preferences_page::guid_display`（显示类）、`guid_components`（组件类）
- 组件入口注册方式：`FB2K_SERVICE_FACTORY` 宏（参照 `component_entry.mm`）

### 既有渲染参数（LyricView）
- 硬编码在 `LyricView.mm` 中：`kNormalFontSize=14`、`kHighlightFontSize=16`、`kNormalColor`、`kHighlightColor` 等
- 目前不支持外部配置字体字号颜色
- 行距、对齐方式固定在 drawRect 实现中

### 既有源管线（LyricPanelController）
- 五级管线硬编码在 `handleTrackChanged` 中：Tag → Local → LrcLib → NetEase → QQMusic
- 失效隔离计数器 `_lrclibFailures/_neteaseFailures/_qqmusicFailures` 硬编码上限 5
- HttpAdapter 超时硬编码 11s（`HttpAdapter.mm` 中 `kTimeoutSec=11`）

### 既有落盘路径
- LyricStore::save() 写 `<音频文件所在目录>/<音频名>.lrc`
- 路径模板不可配置

## 设计决策

### 配置模型（core/config/AppConfig.h）
```cpp
struct SourceConfig {
    std::string key;     // "tag"/"local"/"lrclib"/"netease"/"qqmusic"
    bool enabled = true;
};

struct DisplayConfig {
    std::string fontName = "System";     // 或 "Monaco"/"PingFang SC" 等
    double fontSize = 14.0;
    double highlightScale = 1.15;        // 高亮行放大倍率
    std::string normalColor = "#333333";
    std::string highlightColor = "#007AFF";
    std::string alignment = "center";     // left/center/right
    double lineSpacing = 6.0;
};

struct AppConfig {
    std::vector<SourceConfig> sources;   // 顺序即管线优先级
    DisplayConfig display;
    int64_t defaultOffsetMs = 0;
    int httpTimeoutSec = 11;
    int maxConsecutiveFailures = 5;
    std::string savePathTemplate = "";   // 空=同目录；"{artist}/{album}/" 等
    std::string logLevel = "info";       // debug/info/warn/error
};
```

### 配置持久化
- NSUserDefaults 键 `foo_openlyrics_config` 存 JSON 字符串
- core 层 AppConfig 提供 `toJson()`/`fromJson()` 方法（手动拼接，不引三方 JSON 库）
- 平台层 ConfigAdapter 封装 NSUserDefaults 读写，提供 `load(AppConfig&)` / `save(AppConfig&)`
- 首次启动无配置时返回 AppConfig 默认值

### 偏好设置页
- 单一 NSViewController，用 NSTabView 分三个 tab：Sources / Display / Advanced
- Sources tab：NSTableView（source key + enabled checkbox），支持拖拽排序
- Display tab：字体按钮（NSFontPanel）、颜色井（NSColorWell）、对齐弹出按钮（NSPopUpButton）、行距滑块（NSSlider）
- Advanced tab：默认 offset（NSTextField 数字输入）、超时秒数（NSTextField）、最大连续失败次数（NSTextField）、日志级别（NSPopUpButton）
- 父 GUID 用 `preferences_page::guid_display`

### 内置编辑器
- LyricPanelController 新增编辑模式切换按钮（铅笔图标/文字按钮）
- 编辑模式下：NSTextView 替换 LyricView 的显示位置，内容为 sourceText 原始文本
- "完成"按钮：LrcParser::parse 编辑文本 → 校验至少有一行文本 → 更新 LyricData → forceSave 到 .lrc → 切回视图模式，LyricView 展示新歌词
- "取消"按钮：丢弃编辑切回视图模式
- 仅当有歌词时显示编辑按钮（`_currentLyricData.lines` 非空）

## Task 1: AppConfig 核心模型 + ConfigAdapter 平台层

**文件：**
- 新增 `core/config/AppConfig.h`：SourceConfig/DisplayConfig/AppConfig 结构体 + `toJson()`/`fromJson()`
- 新增 `core/config/AppConfig.cpp`：JSON 序列化/反序列化实现（手写，用 `JsonField.h` 已有工具）
- 新增 `platform/ConfigAdapter.h`：NSUserDefaults 读写封装
- 新增 `platform/ConfigAdapter.mm`：load/save 实现

**测试：**
- test_app_config.cpp：
  - DefaultConfig：默认构造含 5 个 source 且全部 enabled
  - RoundTrip：toJson → fromJson 一致性
  - JsonParseEmpty：空字符串返回默认值
  - SourceOrderPreserved：序列化/反序列化保持 source 顺序

## Task 2: 偏好设置页 UI

**文件：**
- 新增 `ui/PreferencesViewController.h`：NSViewController 子类，含 NSTabView
- 新增 `ui/PreferencesViewController.mm`：三个 tab 的控件布局与交互
- 修改 `platform/component_entry.mm`：注册 preferences_page 服务

**Sources tab 实现：**
1. NSTableView 单列（source 显示名），checkbox 列勾选启用/禁用
2. 支持拖拽重排（tableView:writeRowsWithIndexes:/validateDrop 等 delegate）
3. 变更即时写回 `_config.sources`

**Display tab 实现：**
1. 字体选择：NSButton 触发 `[NSFontPanel sharedFontPanel]`，当前字体名+字号显示在标签
2. 常规色/高亮色：各一个 NSColorWell
3. 对齐：NSPopUpButton（左/中/右）
4. 行距：NSSlider（0-20pt）+ 数值标签
5. 预览：一个静态 NSTextField 显示"歌词行预览"用当前显示设置渲染

**Advanced tab 实现：**
1. 默认 offset：NSTextField（整数，毫秒）
2. HTTP 超时：NSTextField（1-60s）
3. 最大连续失败次数：NSTextField（1-50）
4. 日志级别：NSPopUpButton（debug/info/warn/error）

**注册（component_entry.mm）：**
```cpp
class openlyrics_preferences_page : public preferences_mac_common<PreferencesViewController> {
public:
    const char* get_name() override { return "OpenLyrics"; }
    GUID get_guid() override {
        // 固定 GUID
        return { 0x... };
    }
    GUID get_parent_guid() override {
        return preferences_page::guid_display;
    }
};
FB2K_SERVICE_FACTORY(openlyrics_preferences_page);
```

## Task 3: 面板编辑模式

**文件：** 修改 `ui/LyricPanelController.mm`

**实现：**
1. 在 offset 控件组旁边新增"编辑"按钮（NSButton，title="编辑"）
2. 点击"编辑"→ 隐藏 LyricView，显示 NSTextView（等宽字体，填满 LyricView 原有区域）
3. NSTextView 内容为 `_currentLyricData.sourceText`（空则用 LrcSerializer::serialize 生成）
4. 底部出现"完成"/"取消"两个按钮
5. "完成"：取 NSTextView 文本 → LrcParser::parse → 非空 → 更新 _currentLyricData → forceSave → 切回视图
6. "取消"：丢弃编辑 → 切回视图
7. 约束动画：隐藏/显示 LyricView 和 NSTextView 互斥

**NSTextView 配置：**
- 等宽字体（Monaco 或 Menlo, 12pt）
- 禁用自动纠错/拼写检查
- 禁用智能引号/破折号替换

## Task 4: 配置接线——面板与适配器打通

**文件：** 修改 `ui/LyricPanelController.mm`、`ui/LyricView.h/.mm`、`platform/HttpAdapter.mm`

**源顺序接线：**
1. LyricPanelController 启动时从 ConfigAdapter 加载 AppConfig
2. handleTrackChanged 中的五级管线改为按 config.sources 顺序动态构建
3. 失效隔离计数器改为 `std::map<std::string, int>`（key = source key）

**显示配置接线：**
1. LyricView 新增 `-applyDisplayConfig:` 方法，接受 `DisplayConfig` 结构
2. setLyricData 被调用时用当前字体/颜色重建缓存富文本
3. 不再硬编码 kNormalFontSize 等常量

**其他配置接线：**
1. HttpAdapter 新增 `setTimeout(int sec)` 方法
2. maxConsecutiveFailures 从 config 读取
3. tickSync 的 extraOffsetMs 默认值改用 config.defaultOffsetMs + UI offset（叠加）

## 任务依赖

```
Task 1 (AppConfig + ConfigAdapter)
  ├──> Task 2 (偏好设置页 UI)
  └──> Task 4 (配置接线)

Task 3 (编辑模式) —— 独立，可与 Task 2/4 并行
```

## 全局约束

- 纯 C++ 核心仅 std-lib，命名空间 `openlyrics`，禁 SDK/AppKit/ObjC
- 平台层 Obj-C++ ARC
- 不破坏既有 115 项测试与 fb2k_sdk/foo_openlyrics 目标
- 提交信息简体中文动宾式
- UI 层人工验证
