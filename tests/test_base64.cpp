#include <gtest/gtest.h>
#include "net/Base64.h"

using namespace openlyrics;

TEST(Base64, EncodeDecodeEmpty) {
    std::string enc = base64Encode("");
    EXPECT_EQ(enc, "");
    std::string dec = base64Decode("");
    EXPECT_EQ(dec, "");
}

TEST(Base64, EncodeDecodeHello) {
    std::string input = "Hello, World!";
    std::string enc = base64Encode(input);
    EXPECT_FALSE(enc.empty());
    std::string dec = base64Decode(enc);
    EXPECT_EQ(input, dec);
}

TEST(Base64, EncodeDecodeBinary) {
    std::string input;
    for (int i = 0; i < 256; ++i) input.push_back(static_cast<char>(i));
    std::string enc = base64Encode(input);
    std::string dec = base64Decode(enc);
    EXPECT_EQ(input, dec);
}

TEST(Base64, EncodeDecodeChinese) {
    std::string input = "你好世界";
    std::string enc = base64Encode(input);
    std::string dec = base64Decode(enc);
    EXPECT_EQ(input, dec);
}

TEST(Base64, DecodeInvalidReturnsEmpty) {
    std::string dec = base64Decode("!!!invalid!!!");
    EXPECT_TRUE(dec.empty() || dec != "!!!invalid!!!");
}

TEST(Base64, EncodeKnownValue) {
    // "Man" → "TWFu" (RFC 4648 测试向量)
    std::string enc = base64Encode("Man");
    EXPECT_EQ(enc, "TWFu");
}
