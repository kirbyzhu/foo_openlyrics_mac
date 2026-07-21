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
    EXPECT_FALSE(d.synced);
}

TEST(LrcParser, HugeMinutesNoOverflow) {
    LyricData d;
    EXPECT_NO_THROW({ d = LrcParser::parse("[999999999999999:00]x"); });
    // 分钟值经算术放大会溢出 int64，必须被判为非法时标而非产生同步行
    EXPECT_FALSE(d.synced);
}

TEST(LrcParser, NegativeOffsetParsed) {
    LyricData d = LrcParser::parse("[offset:-500]\n[00:00.00]z");
    EXPECT_EQ(d.offsetMs, -500);
}

TEST(LrcParser, MultipleTimestampsExpandToLines) {
    LyricData d = LrcParser::parse("[00:01.00][00:03.00]repeat");
    ASSERT_EQ(d.lines.size(), 2u);
    EXPECT_EQ(d.lines[0].timeMs, 1000);
    EXPECT_EQ(d.lines[1].timeMs, 3000);
    EXPECT_EQ(d.lines[0].text, "repeat");
    EXPECT_EQ(d.lines[1].text, "repeat");
}

TEST(LrcParser, OffsetTagParsed) {
    LyricData d = LrcParser::parse("[offset:-500]\n[00:02.00]line");
    EXPECT_EQ(d.offsetMs, -500);
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].timeMs, 2000);
}

TEST(LrcParser, IdTagsCollected) {
    LyricData d = LrcParser::parse("[ti:Song]\n[ar:Artist]\n[00:00.00]x");
    ASSERT_EQ(d.tags.size(), 2u);
    EXPECT_EQ(d.tags[0].first, "ti");
    EXPECT_EQ(d.tags[0].second, "Song");
    EXPECT_EQ(d.tags[1].first, "ar");
    EXPECT_EQ(d.tags[1].second, "Artist");
}

TEST(LrcParser, LinesSortedAscending) {
    LyricData d = LrcParser::parse("[00:05.00]b\n[00:01.00]a");
    ASSERT_EQ(d.lines.size(), 2u);
    EXPECT_EQ(d.lines[0].text, "a");
    EXPECT_EQ(d.lines[1].text, "b");
}

TEST(LrcParser, MalformedBracketTreatedAsText) {
    LyricData d = LrcParser::parse("[not a time]still text");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_FALSE(d.synced);
    EXPECT_EQ(d.lines[0].timeMs, -1);
    // [not a time] 含冒号，被当作 id 标签 key="not a time"，正文为剩余
    EXPECT_EQ(d.lines[0].text, "still text");
}

TEST(LrcParser, EmptyInput) {
    LyricData d = LrcParser::parse("");
    EXPECT_TRUE(d.lines.empty());
    EXPECT_FALSE(d.synced);
}

TEST(LrcParser, ThreeDigitFraction) {
    LyricData d = LrcParser::parse("[00:01.005]x");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].timeMs, 1005);
}
