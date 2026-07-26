#include "search/PinyinPolyphonic.h"

#include <unordered_map>

namespace openlyrics {
namespace {
const std::unordered_map<char32_t, std::vector<std::string>>& table() {
    static const std::unordered_map<char32_t, std::vector<std::string>> t = {
        {U'行', {"xing", "hang"}},
        {U'长', {"chang", "zhang"}},
        {U'重', {"zhong", "chong"}},
        {U'乐', {"le", "yue"}},
        {U'中', {"zhong"}},
        {U'曲', {"qu"}},
        {U'调', {"diao", "tiao"}},
        {U'藏', {"cang", "zang"}},
        {U'都', {"dou", "du"}},
        {U'弹', {"tan", "dan"}},
        {U'和', {"he", "huo", "hai", "huan"}},
        {U'降', {"jiang", "xiang"}},
        {U'空', {"kong"}},
        {U'着', {"zhe", "zhao", "zhuo"}},
        {U'觉', {"jue", "jiao"}},
        {U'血', {"xue", "xie"}},
        {U'落', {"luo", "lao", "la"}},
        {U'背', {"bei"}},
        {U'为', {"wei"}},
        {U'干', {"gan"}},
    };
    return t;
}
}  // namespace

const std::vector<std::string>* polyphonicReadings(char32_t cp) {
    const auto& t = table();
    auto it = t.find(cp);
    return it == t.end() ? nullptr : &it->second;
}
}  // namespace openlyrics
