#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace openlyrics {

struct DeskLyricsConfig {
    bool enabled = false;
    bool showOnlyInBackground = true;
    double fontSize = 28.0;
    std::string normalColor = "#FFFFFF";
    std::string highlightColor = "#FFD700";
    std::string titleColor = "#FFFFFF";  // 标题栏文字颜色
    std::string alignment = "center";
    double lineSpacing = 8.0;
    double windowWidth = 600.0;
    double windowHeight = 120.0;
    double windowX = -1;   // -1 表示下次启动时自动居中
    double windowY = -1;
    int maxLines = 3;       // 显示行数，取值 3–7
    bool showTitle = true;  // 顶部显示「歌名 — 艺术家」标题栏
};

struct SourceConfig {
    std::string key;     // "tag"/"local"/"lrclib"/"netease"/"qqmusic"
    bool enabled = true;
};

struct DisplayConfig {
    std::string fontName = "System";
    double fontSize = 14.0;
    double highlightScale = 1.15;
    std::string normalColor = "#333333";
    std::string highlightColor = "#007AFF";
    std::string titleColor = "#FFFFFF";  // 桌面歌词标题栏颜色，内嵌面板不使用
    std::string alignment = "center";   // "left"/"center"/"right"
    double lineSpacing = 6.0;
};

struct AppConfig {
    std::vector<SourceConfig> sources;
    DisplayConfig display;
    int64_t defaultOffsetMs = 0;
    int httpTimeoutSec = 11;
    int maxConsecutiveFailures = 5;
    std::string savePathTemplate;
    std::string logLevel = "info";
    DeskLyricsConfig deskLyrics;

    // 序列化为 JSON 字符串；反序列化失败（空串/格式错误）返回默认值。
    std::string toJson() const;
    static AppConfig fromJson(const std::string& json);

    // 默认五级管线配置
    static AppConfig defaults();
};

}  // namespace openlyrics
