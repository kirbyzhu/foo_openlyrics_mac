#include <gtest/gtest.h>
#include "parser/LrcParser.h"

using namespace openlyrics;

TEST(LrcParser, SingleTimestampLine) {
    LyricData d = LrcParser::parse("[00:12.34]Hello world");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_TRUE(d.synced);
    EXPECT_EQ(d.lines[0].timeMs, 12340);
    EXPECT_EQ(d.lines[0].text, "Hello world");
}

TEST(LrcParser, PlainTextFallback) {
    LyricData d = LrcParser::parse("just a line\nanother line");
    ASSERT_EQ(d.lines.size(), 2u);
    EXPECT_FALSE(d.synced);
    EXPECT_EQ(d.lines[0].timeMs, -1);
    EXPECT_EQ(d.lines[0].text, "just a line");
    EXPECT_EQ(d.lines[1].text, "another line");
}

TEST(LrcParser, MalformedOffsetIgnoredNoThrow) {
    LyricData d;
    EXPECT_NO_THROW({ d = LrcParser::parse("[offset:auto]\n[00:01.00]x"); });
    EXPECT_EQ(d.offsetMs, 0);
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].timeMs, 1000);
}

TEST(LrcParser, EmptyOffsetIgnoredNoThrow) {
    LyricData d;
    EXPECT_NO_THROW({ d = LrcParser::parse("[offset:]\n[00:02.00]y"); });
    EXPECT_EQ(d.offsetMs, 0);
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].timeMs, 2000);
}

TEST(LrcParser, ValidSignedOffsetParsed) {
    LyricData d = LrcParser::parse("[offset:+250]\n[00:00.00]z");
    EXPECT_EQ(d.offsetMs, 250);
}

TEST(LrcParser, OverlongOffsetIgnoredNoThrow) {
    LyricData d;
    EXPECT_NO_THROW({ d = LrcParser::parse("[offset:99999999999999999999]\n[00:01.00]x"); });
    EXPECT_EQ(d.offsetMs, 0);
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].timeMs, 1000);
}

TEST(LrcParser, OverlongMinutesTimeTagNoThrow) {
    LyricData d;
    EXPECT_NO_THROW({ d = LrcParser::parse("[99999999999999999999:12.34]text"); });
    // 该括号不是合法时标，不得产生带时标的歌词行
    for (const auto& line : d.lines) {
        EXPECT_LT(line.timeMs, 0);
    }
}
