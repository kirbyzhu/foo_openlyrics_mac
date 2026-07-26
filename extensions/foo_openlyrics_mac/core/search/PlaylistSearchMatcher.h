#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace openlyrics {

// 一个源字符对应一个 cell。alternatives 为该字符的全拼候选（小写，非空）；
// 拉丁/数字为单字符字符串；汉字为其全部读音。initials 为各 alternative 的首字符去重。
struct SearchCell {
    std::vector<std::string> alternatives;
    std::vector<char> initials;
};

using SearchField = std::vector<SearchCell>;

struct SearchRecord {
    SearchField title;
    SearchField artist;
    SearchField album;
};

struct MatchHit {
    std::size_t index;
    int score;
};

// 未命中返回 -1；空 query 返回 0。导出以便单测。
int scoreField(const SearchField& field, const std::string& query);

// query 由调用方 trim + 转小写。空 query 命中全部（score 0，保持原序）。
std::vector<MatchHit> matchPlaylist(const std::vector<SearchRecord>& records,
                                    const std::string& query);

}  // namespace openlyrics
