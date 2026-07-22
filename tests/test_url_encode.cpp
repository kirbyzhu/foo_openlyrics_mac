#include <gtest/gtest.h>
#include "net/UrlEncode.h"

using namespace openlyrics;

TEST(UrlEncodeTest, EmptyString) {
    EXPECT_EQ("", urlEncodeComponent(""));
}

TEST(UrlEncodeTest, UnreservedPassthrough) {
    EXPECT_EQ("Abc-1_2.3~", urlEncodeComponent("Abc-1_2.3~"));
}

TEST(UrlEncodeTest, SpaceToPercent20) {
    EXPECT_EQ("a%20b", urlEncodeComponent("a b"));
}

TEST(UrlEncodeTest, SpecialChars) {
    EXPECT_EQ("a%26b%3Dc%3F", urlEncodeComponent("a&b=c?"));
}

TEST(UrlEncodeTest, ChineseUtf8ByteWise) {
    // 伍佰 in UTF-8: E4 BC 8D E4 BD B0
    EXPECT_EQ("%E4%BC%8D%E4%BD%B0", urlEncodeComponent("伍佰"));
}

TEST(UrlEncodeTest, UppercaseHex) {
    // Newline \n is 0x0A
    EXPECT_EQ("%0A", urlEncodeComponent("\n"));
}
