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
    ASSERT_EQ(d.lines.size(), 1u);   // 两个纯 id 标签行不产出歌词行，仅保留时标行
    EXPECT_EQ(d.lines[0].timeMs, 0);
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
    // [not a time] 无冒号，既非时标也非 id 标签，仅被消费；因剩余内容非空而产出 untimed 行
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

TEST(LrcParser, TagLineTrailingWhitespaceNoSpuriousLine) {
    LyricData d = LrcParser::parse("[ti:Song] \n[00:01.00]x");
    ASSERT_EQ(d.lines.size(), 1u);   // 标签行尾随空白不得产出伪造空白行
    EXPECT_EQ(d.lines[0].timeMs, 1000);
    EXPECT_EQ(d.lines[0].text, "x");
}

TEST(LrcParser, StripsInlineWordTimestamps) {
    LyricData d = LrcParser::parse(
        "[00:01.07]突[00:01.58]然[00:01.79]的[00:02.00]自[00:02.16]我");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].timeMs, 1070);
    EXPECT_EQ(d.lines[0].text, "突然的自我");
}

TEST(LrcParser, StripsTrailingTimestamp) {
    LyricData d = LrcParser::parse("[00:30.70]词：徐克[00:30.70]");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].timeMs, 30700);
    EXPECT_EQ(d.lines[0].text, "词：徐克");
}

TEST(LrcParser, InlineStripKeepsNonTimeBrackets) {
    LyricData d = LrcParser::parse("[00:01.00]hello [world] ok");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].text, "hello [world] ok");
}

TEST(LrcParser, InlineStripKeepsSpacesBetweenWords) {
    LyricData d = LrcParser::parse("[00:01.00]我 [00:02.36]- [00:02.58]伍");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].text, "我 - 伍");
}

TEST(LrcParser, StandardLineTextUnaffectedByInlineStrip) {
    LyricData d = LrcParser::parse("[00:12.34]Hello world");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].text, "Hello world");
}

TEST(LrcParser, MultiLeadingTimestampsStillExpandWithInlineStrip) {
    LyricData d = LrcParser::parse("[00:01.00][00:03.00]repeat");
    ASSERT_EQ(d.lines.size(), 2u);
    EXPECT_EQ(d.lines[0].text, "repeat");
    EXPECT_EQ(d.lines[1].text, "repeat");
}

TEST(LrcParser, SourceTextCapturesRawInput) {
    LyricData d = LrcParser::parse("[00:01.00]a");
    EXPECT_EQ(d.sourceText, "[00:01.00]a");
}

TEST(LrcParser, SourceTextEmptyForEmptyInput) {
    LyricData d = LrcParser::parse("");
    EXPECT_EQ(d.sourceText, "");
}

// 网易云 YRC 逐字格式行：{"t":ms,"c":[{"tx":".."},..]} 应被解析为带时标的标准行，
// 时标取行首 t（毫秒），文本为所有 tx 拼接。
TEST(LrcParser, YrcMetadataLine) {
    LyricData d = LrcParser::parse(
        "{\"t\":0,\"c\":[{\"tx\":\"作词: \"},{\"tx\":\"Stuart Gotz\"}]}");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].timeMs, 0);
    EXPECT_EQ(d.lines[0].text, "作词: Stuart Gotz");
}

TEST(LrcParser, YrcLineWithNonZeroTime) {
    LyricData d = LrcParser::parse(
        "{\"t\":1000,\"c\":[{\"tx\":\"作曲: \"},{\"tx\":\"Stuart Gotz\"}]}");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].timeMs, 1000);
    EXPECT_EQ(d.lines[0].text, "作曲: Stuart Gotz");
}

TEST(LrcParser, YrcMixedWithStandardLrc) {
    // 网易云常见：YRC 元数据行在前，标准 LRC 在后
    LyricData d = LrcParser::parse(
        "{\"t\":0,\"c\":[{\"tx\":\"作词: \"},{\"tx\":\"A\"}]}\n"
        "[00:08.31]We come on the sloop John B");
    ASSERT_EQ(d.lines.size(), 2u);
    EXPECT_EQ(d.lines[0].timeMs, 0);
    EXPECT_EQ(d.lines[0].text, "作词: A");
    EXPECT_EQ(d.lines[1].timeMs, 8310);
    EXPECT_EQ(d.lines[1].text, "We come on the sloop John B");
    EXPECT_TRUE(d.synced);
}

TEST(LrcParser, NonYrcBraceLineStaysPlain) {
    // 不是 YRC 结构的花括号文本行保持原样（不误判）
    LyricData d = LrcParser::parse("{not yrc}");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].timeMs, -1);
    EXPECT_EQ(d.lines[0].text, "{not yrc}");
}
