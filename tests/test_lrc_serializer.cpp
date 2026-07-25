#include <gtest/gtest.h>
#include "parser/LrcSerializer.h"
#include "parser/LrcParser.h"

using namespace openlyrics;

TEST(LrcSerializer, FormatsTimestamp) {
    LyricData d;
    d.synced = true;
    d.lines.push_back({12340, "Hello", {}});
    std::string out = LrcSerializer::serialize(d);
    EXPECT_EQ(out, "[00:12.34]Hello\n");
}

TEST(LrcSerializer, WritesTagsAndOffset) {
    LyricData d;
    d.tags = {{"ti", "Song"}};
    d.offsetMs = -500;
    d.synced = true;
    d.lines.push_back({0, "x", {}});
    std::string out = LrcSerializer::serialize(d);
    EXPECT_EQ(out, "[ti:Song]\n[offset:-500]\n[00:00.00]x\n");
}

TEST(LrcSerializer, RoundTrip) {
    std::string src = "[00:01.50]a\n[00:03.00]b\n";
    LyricData d = LrcParser::parse(src);
    EXPECT_EQ(LrcSerializer::serialize(d), src);
}

TEST(LrcSerializer, PlainTextLinesNoTimestamp) {
    LyricData d;
    d.lines.push_back({-1, "just text", {}});
    EXPECT_EQ(LrcSerializer::serialize(d), "just text\n");
}

TEST(LrcSerializer, SerializeSyllablesAsInlineTimeTags) {
    LyricData d;
    d.synced = true;
    LyricLine line;
    line.timeMs = 1070;
    line.text = "突然的自我";
    line.syllables = {
        {1070, 1580, "突"},
        {1580, 1790, "然"},
        {1790, 2000, "的"},
        {2000, 2160, "自"},
        {2160, 0, "我"}
    };
    d.lines.push_back(line);

    std::string out = LrcSerializer::serialize(d);
    EXPECT_EQ(out, "[00:01.07]突[00:01.58]然[00:01.79]的[00:02.00]自[00:02.16]我\n");
}

TEST(LrcSerializer, SyllablesRoundTrip) {
    std::string src = "[00:01.07]突[00:01.58]然[00:01.79]的[00:02.00]自[00:02.16]我\n";
    LyricData d = LrcParser::parse(src);
    EXPECT_EQ(LrcSerializer::serialize(d), src);
}

// 行级无时标（timeMs<0）时，首音节须补发自身时标，否则首音节起始时刻丢失。
TEST(LrcSerializer, SyllableLineWithoutLineTimeEmitsFirstTag) {
    LyricData d;
    LyricLine line;
    line.timeMs = -1;
    line.syllables = {
        {0, 500, "a"},
        {500, 0, "b"}
    };
    d.lines.push_back(line);

    EXPECT_EQ(LrcSerializer::serialize(d), "[00:00.00]a[00:00.50]b\n");
}
