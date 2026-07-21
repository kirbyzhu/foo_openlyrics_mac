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
