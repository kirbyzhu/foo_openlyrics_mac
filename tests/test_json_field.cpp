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

TEST(JsonFieldTest, GetIntPositive) {
    int64_t out = 0;
    EXPECT_TRUE(jsonGetInt(R"({"v":42})", "v", out));
    EXPECT_EQ(out, 42);
}

TEST(JsonFieldTest, GetIntNegative) {
    int64_t out = 0;
    EXPECT_TRUE(jsonGetInt(R"({"v":-128})", "v", out));
    EXPECT_EQ(out, -128);
}

TEST(JsonFieldTest, GetIntZero) {
    int64_t out = 1;
    EXPECT_TRUE(jsonGetInt(R"({"v":0})", "v", out));
    EXPECT_EQ(out, 0);
}

TEST(JsonFieldTest, GetIntLarge) {
    int64_t out = 0;
    EXPECT_TRUE(jsonGetInt(R"({"v":9223372036854775807})", "v", out));
    EXPECT_EQ(out, INT64_MAX);
}

TEST(JsonFieldTest, GetIntOverflowReturnsFalse) {
    int64_t out = 0;
    EXPECT_FALSE(jsonGetInt(R"({"v":99999999999999999999})", "v", out));
}

TEST(JsonFieldTest, GetIntNotANumberReturnsFalse) {
    int64_t out = 0;
    EXPECT_FALSE(jsonGetInt(R"({"v":"hello"})", "v", out));
}

TEST(JsonFieldTest, GetIntMissingKeyReturnsFalse) {
    int64_t out = 0;
    EXPECT_FALSE(jsonGetInt(R"({"x":1})", "v", out));
}

TEST(JsonFieldTest, GetObjectSuccess) {
    std::string out;
    EXPECT_TRUE(jsonGetObject(R"({"o":{"a":1,"b":2}})", "o", out));
    EXPECT_EQ(out, R"({"a":1,"b":2})");
}

TEST(JsonFieldTest, GetObjectEmpty) {
    std::string out;
    EXPECT_TRUE(jsonGetObject(R"({"o":{}})", "o", out));
    EXPECT_EQ(out, "{}");
}

TEST(JsonFieldTest, GetObjectNested) {
    std::string out;
    EXPECT_TRUE(jsonGetObject(R"({"o":{"inner":{"x":1}}})", "o", out));
    EXPECT_EQ(out, R"({"inner":{"x":1}})");
}

TEST(JsonFieldTest, GetObjectMissingKeyReturnsFalse) {
    std::string out;
    EXPECT_FALSE(jsonGetObject(R"({"a":1})", "o", out));
}

TEST(JsonFieldTest, GetObjectNotAnObjectReturnsFalse) {
    std::string out;
    EXPECT_FALSE(jsonGetObject(R"({"o":42})", "o", out));
}

TEST(JsonFieldTest, ExtractObjectSimple) {
    std::string json = R"({"a":1,"b":2})";
    size_t pos = 0;
    std::string out;
    EXPECT_TRUE(jsonExtractObject(json, pos, out));
    EXPECT_EQ(out, json);
}

TEST(JsonFieldTest, ExtractObjectSkipsStringBraces) {
    std::string json = R"({"a":"foo{bar}baz","c":1}{"next":2})";
    size_t pos = 0;
    std::string out;
    EXPECT_TRUE(jsonExtractObject(json, pos, out));
    EXPECT_EQ(out, R"({"a":"foo{bar}baz","c":1})");
    // pos should point past the first object
    EXPECT_EQ(json[pos], '{');
}

TEST(JsonFieldTest, UnknownEscapeKeepsLiteralChar) {
    std::string out;
    EXPECT_TRUE(jsonGetString(R"({"a":"weird \x escape"})", "a", out));
    EXPECT_EQ("weird x escape", out);
}

TEST(JsonFieldTest, EscapeQuotesAndBackslash) {
    EXPECT_EQ(jsonEscapeString(R"(say "hi")"), R"(say \"hi\")");
    EXPECT_EQ(jsonEscapeString(R"(a\b)"), R"(a\\b)");
}

TEST(JsonFieldTest, EscapeControlChars) {
    EXPECT_EQ(jsonEscapeString("a\nb\tc"), "a\\nb\\tc");
    EXPECT_EQ(jsonEscapeString(std::string("\x01", 1)), "\\u0001");
}

TEST(JsonFieldTest, EscapePassesThroughUtf8AndPlainText) {
    EXPECT_EQ(jsonEscapeString("晴天"), "晴天");
    EXPECT_EQ(jsonEscapeString("normal text 123"), "normal text 123");
}

// 转义后嵌入 JSON 字面量应可被 jsonGetString 无损取回。
TEST(JsonFieldTest, EscapeRoundTrips) {
    std::string raw = R"(Godzilla "King" \n of monsters)";
    std::string json = "{\"s\":\"" + jsonEscapeString(raw) + "\"}";
    std::string out;
    ASSERT_TRUE(jsonGetString(json, "s", out));
    EXPECT_EQ(out, raw);
}
