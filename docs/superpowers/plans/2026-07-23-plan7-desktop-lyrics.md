# 计划七：桌面歌词（浮动透明窗口）

最后更新 2026-07-23。

## 一、目标

foobar2000 切换到后台播放时，在前台屏幕上显示一个透明浮动窗口实时同步展示歌词。

## 二、架构

```
DesktopLyricsController (NSObject 单例)
  ├── NSPanel (borderless, floating, transparent, all-Spaces)
  │     └── DeskLyricsContent (mouseDownCanMoveWindow=YES, 拖拽移动)
  │           └── LyricView (transparentBackground=YES, 独立 DeskLyricsConfig)
  ├── 订阅 PlaybackHub → handleTrackChanged (独立搜索管线)
  ├── NSTimer (60ms tick → SyncEngine::locate → LyricView.setSyncResult)
  ├── NSApplication.didResignActiveNotification → showPanel
  └── NSApplication.didBecomeActiveNotification → hidePanel
```

## 三、关键设计决策

1. 搜索管线独立复制（~100 行），与 LyricPanelController 不共享。两个控制器 UI 更新需求不同、失败计数器需独立。
2. NSPanel 不设 `ignoresMouseEvents`。`movableByWindowBackground` + `mouseDownCanMoveWindow` 支持拖拽。
3. 窗口位置不做持久化，默认出现在屏幕水平居中、垂直 75% 处。
4. LyricView 新增 `transparentBackground` 属性，为 YES 时不填充背景色。

## 四、文件变更

| 文件 | 操作 | 说明 |
|---|---|---|
| `core/config/AppConfig.h` | 修改 | 新增 `DeskLyricsConfig` 结构体 + `AppConfig::deskLyrics` 字段 |
| `core/config/AppConfig.cpp` | 修改 | 扩展 `toJson()` / `fromJson()` / `defaults()` |
| `ui/DesktopLyricsController.h` | 新建 | 单例 NSObject 接口，实现 PlaybackHubObserving |
| `ui/DesktopLyricsController.mm` | 新建 | NSPanel 创建、PlaybackHub 订阅、独立搜索管线、同步 tick、前后台切换 |
| `ui/LyricView.h` | 修改 | 新增 `transparentBackground` 属性 + `stopAnimation` 方法 |
| `ui/LyricView.mm` | 修改 | `drawRect:` 条件跳过背景填充 + `stopAnimation` 实现 |
| `ui/PreferencesViewController.mm` | 修改 | 新增第 4 tab"桌面歌词"，含 7 项控件 |
| `platform/PlaybackBridge.mm` | 修改 | `on_playback_new_track` 首次触发时懒初始化 DesktopLyricsController |
| `CMakeLists.txt` | 修改 | 添加 DesktopLyricsController.mm 编译与 ARC 标记 |
| `tests/test_app_config.cpp` | 修改 | 新增 4 项 DeskLyricsConfig 测试 |

## 五、Task 列表

- Task 1：AppConfig 扩展（DeskLyricsConfig 模型 + 序列化 + 4 项测试）
- Task 2：DesktopLyricsController（NSPanel + PlaybackHub + 搜索管线 + sync tick）
- Task 3：偏好设置页"桌面歌词"tab
- Task 4：组件入口接线（PlaybackBridge.mm 懒初始化 + 构建验证）
