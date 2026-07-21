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
