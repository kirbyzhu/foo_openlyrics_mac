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
    std::string alignment = "center";
    double lineSpacing = 8.0;
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
