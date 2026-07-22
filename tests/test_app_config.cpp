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
}

TEST(AppConfig, JsonEscapesQuotes) {
    AppConfig c = AppConfig::defaults();
    c.display.fontName = "Font\"Name";
    std::string json = c.toJson();
    // 不应包含未转义引号导致的 JSON 语法错误
    AppConfig c2 = AppConfig::fromJson(json);
    EXPECT_EQ(c2.display.fontName, "Font\"Name");
}
