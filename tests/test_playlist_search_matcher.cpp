#include <gtest/gtest.h>

#include <algorithm>

#include "search/PlaylistSearchMatcher.h"

using namespace openlyrics;

namespace {
// 从小写拉丁串建字段：每字符一个 cell。
SearchField latinField(const std::string& s) {
    SearchField f;
    for (char c : s) {
        if (c == ' ') continue;
        f.push_back(SearchCell{{std::string(1, c)}, {c}});
    }
    return f;
}
// 建一个汉字 cell（多读音）。
SearchCell hanCell(const std::vector<std::string>& readings) {
    SearchCell cell;
    cell.alternatives = readings;
    std::vector<char> ini;
    for (const auto& r : readings)
        if (!r.empty() && std::find(ini.begin(), ini.end(), r[0]) == ini.end())
            ini.push_back(r[0]);
    cell.initials = ini;
    return cell;
}
}  // namespace

TEST(ScoreField, EmptyQueryReturnsZero) {
    EXPECT_EQ(scoreField(latinField("hello"), ""), 0);
}

TEST(ScoreField, ContiguousBeatsSubsequence) {
    int contig = scoreField(latinField("hello"), "hell");
    int sub = scoreField(latinField("hello"), "hlo");
    EXPECT_GT(contig, 0);
    EXPECT_GT(sub, 0);
    EXPECT_GT(contig, sub);
}

TEST(ScoreField, NoMatchReturnsNegative) {
    EXPECT_LT(scoreField(latinField("hello"), "xyz"), 0);
}

TEST(ScoreField, PinyinFullAndInitialsPolyphonic) {
    // "银行"：银 -> yin，行 -> {hang, xing}
    SearchField field{hanCell({"yin"}), hanCell({"hang", "xing"})};
    EXPECT_GE(scoreField(field, "yinhang"), 0);  // 全拼（多音其一）
    EXPECT_GE(scoreField(field, "yinxing"), 0);  // 全拼（另一读音）
    EXPECT_GE(scoreField(field, "yh"), 0);       // 首字母
    EXPECT_GE(scoreField(field, "yx"), 0);       // 首字母（多音）
    EXPECT_LT(scoreField(field, "yz"), 0);       // 无此读音首字母
}

TEST(MatchPlaylist, TitleRanksAboveAlbum) {
    SearchRecord a;  // 命中出现在 album
    a.title = latinField("aaa");
    a.album = latinField("song");
    SearchRecord b;  // 命中出现在 title
    b.title = latinField("song");
    std::vector<SearchRecord> recs{a, b};
    auto hits = matchPlaylist(recs, "song");
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0].index, 1u);  // title 命中排前
}

TEST(MatchPlaylist, EmptyQueryReturnsAllInOrder) {
    std::vector<SearchRecord> recs{SearchRecord{latinField("a")}, SearchRecord{latinField("b")}};
    auto hits = matchPlaylist(recs, "");
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0].index, 0u);
    EXPECT_EQ(hits[1].index, 1u);
}
