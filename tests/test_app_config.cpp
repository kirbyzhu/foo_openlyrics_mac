#include <gtest/gtest.h>
#include "config/AppConfig.h"

using namespace openlyrics;

TEST(AppConfig, DefaultsHasFiveSources) {
    AppConfig c = AppConfig::defaults();
    ASSERT_EQ(c.sources.size(), 5u);
    EXPECT_EQ(c.sources[0].key, "tag");
    EXPECT_TRUE(c.sources[0].enabled);
    EXPECT_EQ(c.sources[4].key, "qqmusic");
}

TEST(AppConfig, RoundTripPreservesSources) {
    AppConfig c = AppConfig::defaults();
    c.sources[2].enabled = false;  // 禁用 lrclib
    c.display.fontSize = 16.0;
    c.defaultOffsetMs = 500;

    std::string json = c.toJson();
    AppConfig c2 = AppConfig::fromJson(json);

    ASSERT_EQ(c2.sources.size(), 5u);
    EXPECT_EQ(c2.sources[0].key, "tag");
    EXPECT_TRUE(c2.sources[0].enabled);
    EXPECT_EQ(c2.sources[2].key, "lrclib");
    EXPECT_FALSE(c2.sources[2].enabled);
    EXPECT_DOUBLE_EQ(c2.display.fontSize, 16.0);
    EXPECT_EQ(c2.defaultOffsetMs, 500);
}

TEST(AppConfig, RoundTripPreservesDisplay) {
    AppConfig c = AppConfig::defaults();
    c.display.fontName = "Monaco";
    c.display.fontSize = 18.0;
    c.display.highlightScale = 1.3;
    c.display.normalColor = "#000000";
    c.display.highlightColor = "#FF0000";
    c.display.alignment = "left";
    c.display.lineSpacing = 8.0;

    std::string json = c.toJson();
    AppConfig c2 = AppConfig::fromJson(json);

    EXPECT_EQ(c2.display.fontName, "Monaco");
    EXPECT_DOUBLE_EQ(c2.display.fontSize, 18.0);
    EXPECT_DOUBLE_EQ(c2.display.highlightScale, 1.3);
    EXPECT_EQ(c2.display.normalColor, "#000000");
    EXPECT_EQ(c2.display.highlightColor, "#FF0000");
    EXPECT_EQ(c2.display.alignment, "left");
    EXPECT_DOUBLE_EQ(c2.display.lineSpacing, 8.0);
}

TEST(AppConfig, FromEmptyJsonReturnsDefaults) {
    AppConfig c = AppConfig::fromJson("");
    ASSERT_EQ(c.sources.size(), 5u);
    EXPECT_TRUE(c.sources[0].enabled);
}

TEST(AppConfig, FromInvalidJsonReturnsDefaults) {
    AppConfig c = AppConfig::fromJson("{garbage");
    ASSERT_EQ(c.sources.size(), 5u);
}

TEST(AppConfig, JsonContainsAllKeys) {
    std::string json = AppConfig::defaults().toJson();
    EXPECT_NE(json.find("\"sources\""), std::string::npos);
    EXPECT_NE(json.find("\"display\""), std::string::npos);
    EXPECT_NE(json.find("\"defaultOffsetMs\""), std::string::npos);
    EXPECT_NE(json.find("\"httpTimeoutSec\""), std::string::npos);
    EXPECT_NE(json.find("\"maxConsecutiveFailures\""), std::string::npos);
    EXPECT_NE(json.find("\"savePathTemplate\""), std::string::npos);
    EXPECT_NE(json.find("\"logLevel\""), std::string::npos);
    EXPECT_NE(json.find("\"deskLyrics\""), std::string::npos);
}

TEST(AppConfig, DeskLyricsDefaultsDisabled) {
    AppConfig c = AppConfig::defaults();
    EXPECT_FALSE(c.deskLyrics.enabled);
    EXPECT_TRUE(c.deskLyrics.showOnlyInBackground);
    EXPECT_DOUBLE_EQ(c.deskLyrics.fontSize, 28.0);
    EXPECT_EQ(c.deskLyrics.normalColor, "#FFFFFF");
    EXPECT_EQ(c.deskLyrics.highlightColor, "#FFD700");
    EXPECT_EQ(c.deskLyrics.titleColor, "#FFFFFF");
    EXPECT_EQ(c.deskLyrics.alignment, "center");
    EXPECT_DOUBLE_EQ(c.deskLyrics.lineSpacing, 8.0);
}

TEST(AppConfig, DeskLyricsRoundTrip) {
    AppConfig c = AppConfig::defaults();
    c.deskLyrics.enabled = true;
    c.deskLyrics.showOnlyInBackground = false;
    c.deskLyrics.fontSize = 32.0;
    c.deskLyrics.normalColor = "#F0F0F0";
    c.deskLyrics.highlightColor = "#00FF00";
    c.deskLyrics.titleColor = "#FF00FF";
    c.deskLyrics.alignment = "left";
    c.deskLyrics.lineSpacing = 12.0;

    std::string json = c.toJson();
    AppConfig c2 = AppConfig::fromJson(json);

    EXPECT_TRUE(c2.deskLyrics.enabled);
    EXPECT_FALSE(c2.deskLyrics.showOnlyInBackground);
    EXPECT_DOUBLE_EQ(c2.deskLyrics.fontSize, 32.0);
    EXPECT_EQ(c2.deskLyrics.normalColor, "#F0F0F0");
    EXPECT_EQ(c2.deskLyrics.highlightColor, "#00FF00");
    EXPECT_EQ(c2.deskLyrics.titleColor, "#FF00FF");
    EXPECT_EQ(c2.deskLyrics.alignment, "left");
    EXPECT_DOUBLE_EQ(c2.deskLyrics.lineSpacing, 12.0);
}

TEST(AppConfig, DeskLyricsJsonKeyPresent) {
    std::string json = AppConfig::defaults().toJson();
    EXPECT_NE(json.find("\"deskLyrics\""), std::string::npos);
}

TEST(AppConfig, DeskLyricsNewFieldsDefaults) {
    AppConfig c = AppConfig::defaults();
    EXPECT_DOUBLE_EQ(c.deskLyrics.windowWidth, 600.0);
    EXPECT_DOUBLE_EQ(c.deskLyrics.windowHeight, 120.0);
    EXPECT_DOUBLE_EQ(c.deskLyrics.windowX, -1.0);
    EXPECT_DOUBLE_EQ(c.deskLyrics.windowY, -1.0);
    EXPECT_EQ(c.deskLyrics.maxLines, 3);
    EXPECT_TRUE(c.deskLyrics.showTitle);
}

TEST(AppConfig, DeskLyricsNewFieldsRoundTrip) {
    AppConfig c = AppConfig::defaults();
    c.deskLyrics.windowWidth = 800.0;
    c.deskLyrics.windowHeight = 200.0;
    c.deskLyrics.windowX = 100.0;
    c.deskLyrics.windowY = 200.0;
    c.deskLyrics.maxLines = 7;
    c.deskLyrics.showTitle = false;

    std::string json = c.toJson();
    AppConfig c2 = AppConfig::fromJson(json);

    EXPECT_DOUBLE_EQ(c2.deskLyrics.windowWidth, 800.0);
    EXPECT_DOUBLE_EQ(c2.deskLyrics.windowHeight, 200.0);
    EXPECT_DOUBLE_EQ(c2.deskLyrics.windowX, 100.0);
    EXPECT_DOUBLE_EQ(c2.deskLyrics.windowY, 200.0);
    EXPECT_EQ(c2.deskLyrics.maxLines, 7);
    EXPECT_FALSE(c2.deskLyrics.showTitle);
}

TEST(AppConfig, DeskLyricsShowTitleDefaultsTrueOnOldConfig) {
    // 旧配置的 deskLyrics 对象缺少 showTitle 字段时应回退默认 true
    AppConfig c = AppConfig::defaults();
    std::string json = c.toJson();
    std::string needle = ",\"showTitle\":true";
    size_t pos = json.find(needle);
    ASSERT_NE(pos, std::string::npos);
    std::string oldJson = json.substr(0, pos) + json.substr(pos + needle.size());

    AppConfig c2 = AppConfig::fromJson(oldJson);
    EXPECT_TRUE(c2.deskLyrics.showTitle);
}

TEST(AppConfig, DeskLyricsClampedOnInvalidSizes) {
    // 窗口尺寸过小或非法时，fromJson 应 clamp 到默认值
    AppConfig c = AppConfig::defaults();
    c.deskLyrics.windowWidth = 50;
    c.deskLyrics.windowHeight = 10;
    c.deskLyrics.maxLines = -5;
    std::string json = c.toJson();
    AppConfig c2 = AppConfig::fromJson(json);
    EXPECT_DOUBLE_EQ(c2.deskLyrics.windowWidth, 600.0);
    EXPECT_DOUBLE_EQ(c2.deskLyrics.windowHeight, 120.0);
    EXPECT_EQ(c2.deskLyrics.maxLines, 3);
}

TEST(AppConfig, DeskLyricsMaxLinesClampedAboveRange) {
    // maxLines 超出 3–7 上界时 fromJson 回退默认 3
    AppConfig c = AppConfig::defaults();
    c.deskLyrics.maxLines = 12;
    AppConfig c2 = AppConfig::fromJson(c.toJson());
    EXPECT_EQ(c2.deskLyrics.maxLines, 3);
}

TEST(AppConfig, DeskLyricsFromOldJsonReturnsDefaults) {
    // 不含 deskLyrics 键的旧 JSON 应回退默认值
    AppConfig c = AppConfig::defaults();
    std::string fullJson = c.toJson();
    size_t pos = fullJson.find(",\"deskLyrics\"");
    ASSERT_NE(pos, std::string::npos);
    std::string oldJson = fullJson.substr(0, pos) + "}";

    AppConfig c2 = AppConfig::fromJson(oldJson);
    EXPECT_FALSE(c2.deskLyrics.enabled);
    EXPECT_DOUBLE_EQ(c2.deskLyrics.fontSize, 28.0);
    EXPECT_EQ(c2.deskLyrics.titleColor, "#FFFFFF");
    EXPECT_DOUBLE_EQ(c2.deskLyrics.windowWidth, 600.0);
    EXPECT_DOUBLE_EQ(c2.deskLyrics.windowHeight, 120.0);
    EXPECT_DOUBLE_EQ(c2.deskLyrics.windowX, -1.0);
    EXPECT_DOUBLE_EQ(c2.deskLyrics.windowY, -1.0);
    EXPECT_EQ(c2.deskLyrics.maxLines, 3);
}

TEST(AppConfig, RoundTripHttpTimeoutSec) {
    AppConfig c = AppConfig::defaults();
    c.httpTimeoutSec = 30;
    std::string json = c.toJson();
    AppConfig c2 = AppConfig::fromJson(json);
    EXPECT_EQ(c2.httpTimeoutSec, 30);
}

TEST(AppConfig, RoundTripMaxConsecutiveFailures) {
    AppConfig c = AppConfig::defaults();
    c.maxConsecutiveFailures = 10;
    std::string json = c.toJson();
    AppConfig c2 = AppConfig::fromJson(json);
    EXPECT_EQ(c2.maxConsecutiveFailures, 10);
}

TEST(AppConfig, RoundTripSavePathTemplate) {
    AppConfig c = AppConfig::defaults();
    c.savePathTemplate = "/music/{artist}/{title}.lrc";
    std::string json = c.toJson();
    AppConfig c2 = AppConfig::fromJson(json);
    EXPECT_EQ(c2.savePathTemplate, "/music/{artist}/{title}.lrc");
}

TEST(AppConfig, RoundTripLogLevel) {
    AppConfig c = AppConfig::defaults();
    c.logLevel = "debug";
    std::string json = c.toJson();
    AppConfig c2 = AppConfig::fromJson(json);
    EXPECT_EQ(c2.logLevel, "debug");
}

TEST(AppConfig, RoundTripDefaultOffsetMs) {
    AppConfig c = AppConfig::defaults();
    c.defaultOffsetMs = -1500;
    std::string json = c.toJson();
    AppConfig c2 = AppConfig::fromJson(json);
    EXPECT_EQ(c2.defaultOffsetMs, -1500);
}

TEST(AppConfig, JsonEscapesQuotes) {
    AppConfig c = AppConfig::defaults();
    c.display.fontName = "Font\"Name";
    std::string json = c.toJson();
    // 不应包含未转义引号导致的 JSON 语法错误
    AppConfig c2 = AppConfig::fromJson(json);
    EXPECT_EQ(c2.display.fontName, "Font\"Name");
}
