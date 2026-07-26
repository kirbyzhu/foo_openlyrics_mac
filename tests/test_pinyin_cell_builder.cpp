#include <gtest/gtest.h>

#include <algorithm>

#include "search/PinyinCellBuilder.h"
#include "search/PinyinPolyphonic.h"

using namespace openlyrics;

namespace {
// 测试用假 lookup：汉字统一给一个固定读音，行/银 特殊处理。
std::vector<std::string> fakeLookup(char32_t cp) {
    if (const auto* p = polyphonicReadings(cp)) return *p;
    if (cp == U'银') return {"yin"};
    return {"x"};  // 其它汉字兜底
}
}  // namespace

TEST(PolyphonicTable, KnownEntry) {
    const auto* r = polyphonicReadings(U'行');
    ASSERT_NE(r, nullptr);
    EXPECT_NE(std::find(r->begin(), r->end(), "hang"), r->end());
    EXPECT_NE(std::find(r->begin(), r->end(), "xing"), r->end());
}

TEST(PolyphonicTable, MissEntry) {
    EXPECT_EQ(polyphonicReadings(U'银'), nullptr);
}

TEST(BuildSearchField, AsciiLowercased) {
    SearchField f = buildSearchField("Hi 9", fakeLookup);
    ASSERT_EQ(f.size(), 3u);  // H i 9，空格跳过
    EXPECT_EQ(f[0].alternatives, (std::vector<std::string>{"h"}));
    EXPECT_EQ(f[2].alternatives, (std::vector<std::string>{"9"}));
}

TEST(BuildSearchField, HanziMultiReading) {
    SearchField f = buildSearchField("银行", fakeLookup);
    ASSERT_EQ(f.size(), 2u);
    EXPECT_EQ(f[0].alternatives, (std::vector<std::string>{"yin"}));
    // 行 -> {xing, hang}
    EXPECT_EQ(f[1].alternatives.size(), 2u);
    // initials 含 x 与 h
    EXPECT_NE(std::find(f[1].initials.begin(), f[1].initials.end(), 'x'), f[1].initials.end());
    EXPECT_NE(std::find(f[1].initials.begin(), f[1].initials.end(), 'h'), f[1].initials.end());
}

TEST(BuildSearchField, PunctuationSkipped) {
    SearchField f = buildSearchField("a-b", fakeLookup);
    ASSERT_EQ(f.size(), 2u);  // '-' 跳过
}
