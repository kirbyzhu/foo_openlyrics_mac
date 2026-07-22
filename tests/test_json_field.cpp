#include <gtest/gtest.h>
#include "net/JsonField.h"

using namespace openlyrics;

TEST(JsonFieldTest, PlainString) {
    std::string out;
    EXPECT_TRUE(jsonGetString(R"({"a":"hello"})", "a", out));
    EXPECT_EQ("hello", out);
}

TEST(JsonFieldTest, NewlineEscape) {
    std::string out;
    EXPECT_TRUE(jsonGetString(R"({"a":"[00:01]a\n[00:02]b"})", "a", out));
    EXPECT_EQ("[00:01]a\n[00:02]b", out);
}

TEST(JsonFieldTest, EscapedQuote) {
    std::string out;
    EXPECT_TRUE(jsonGetString(R"({"a":"he said \"hi\""})", "a", out));
    EXPECT_EQ("he said \"hi\"", out);
}

TEST(JsonFieldTest, UnicodeEscape) {
    std::string out;
    // U+2019 RIGHT SINGLE QUOTATION MARK, UTF-8 bytes E2 80 99
    EXPECT_TRUE(jsonGetString(R"({"a":"’"})", "a", out));
    std::string expected;
    expected.push_back(static_cast<char>(0xE2));
    expected.push_back(static_cast<char>(0x80));
    expected.push_back(static_cast<char>(0x99));
    EXPECT_EQ(expected, out);
}

TEST(JsonFieldTest, SurrogatePair) {
    std::string out;
    // U+1F600 GRINNING FACE = surrogate pair 😀, UTF-8 F0 9F 98 80
    EXPECT_TRUE(jsonGetString(R"({"a":"😀"})", "a", out));
    std::string expected;
    expected.push_back(static_cast<char>(0xF0));
    expected.push_back(static_cast<char>(0x9F));
    expected.push_back(static_cast<char>(0x98));
    expected.push_back(static_cast<char>(0x80));
    EXPECT_EQ(expected, out);
}

TEST(JsonFieldTest, BoolFalse) {
    bool b = true;
    std::string out;
    EXPECT_TRUE(jsonGetBool(R"({"instrumental":false,"x":"y"})", "instrumental", b));
    EXPECT_FALSE(b);
    EXPECT_TRUE(jsonGetString(R"({"instrumental":false,"x":"y"})", "x", out));
    EXPECT_EQ("y", out);
}

TEST(JsonFieldTest, BoolTrue) {
    bool b = false;
    EXPECT_TRUE(jsonGetBool(R"({"instrumental":true})", "instrumental", b));
    EXPECT_TRUE(b);
}

TEST(JsonFieldTest, KeyInsideValueNotFalseMatched) {
    std::string out;
    std::string json = R"({"plainLyrics":"contains \"syncedLyrics\": fake","syncedLyrics":"[00:01]real"})";
    EXPECT_TRUE(jsonGetString(json, "syncedLyrics", out));
    EXPECT_EQ("[00:01]real", out);
}

TEST(JsonFieldTest, MissingKeyReturnsFalse) {
    std::string out;
    EXPECT_FALSE(jsonGetString(R"({"a":"hello"})", "b", out));
}

TEST(JsonFieldTest, NullValueReturnsFalse) {
    std::string out;
    EXPECT_FALSE(jsonGetString(R"({"a":null})", "a", out));
}

TEST(JsonFieldTest, RealisticLrcLibShapedObject) {
    std::string json = R"({"id":12345,"trackName":"Test Track","artistName":"Test Artist",)"
                        R"("albumName":"Test Album","duration":215.5,"instrumental":false,)"
                        R"("plainLyrics":"line one\nline two","syncedLyrics":"[00:01.00]line one\n[00:05.00]line two",)"
                        R"("comment":null})";
    std::string out;
    EXPECT_TRUE(jsonGetString(json, "syncedLyrics", out));
    EXPECT_EQ("[00:01.00]line one\n[00:05.00]line two", out);

    EXPECT_TRUE(jsonGetString(json, "trackName", out));
    EXPECT_EQ("Test Track", out);

    bool instrumental = true;
    EXPECT_TRUE(jsonGetBool(json, "instrumental", instrumental));
    EXPECT_FALSE(instrumental);

    EXPECT_FALSE(jsonGetString(json, "comment", out));
    EXPECT_FALSE(jsonGetString(json, "id", out));  // number, not string
}

TEST(JsonFieldTest, UnknownEscapeKeepsLiteralChar) {
    std::string out;
    EXPECT_TRUE(jsonGetString(R"({"a":"weird \x escape"})", "a", out));
    EXPECT_EQ("weird x escape", out);
}
