#include "search/PinyinCellBuilder.h"

#include <algorithm>
#include <cctype>

namespace openlyrics {
namespace {

// 解码 utf8[i..] 的一个码点，返回码点并把 i 推进到下一字符。非法字节按单字节处理。
char32_t decodeUtf8(const std::string& s, std::size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t n = s.size();
    if (c < 0x80) { ++i; return c; }
    int extra = 0;
    char32_t cp = 0;
    if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
    else { ++i; return c; }
    ++i;
    for (int k = 0; k < extra; ++k) {
        if (i >= n) break;
        unsigned char cc = static_cast<unsigned char>(s[i]);
        if ((cc & 0xC0) != 0x80) break;
        cp = (cp << 6) | (cc & 0x3F);
        ++i;
    }
    return cp;
}

bool isHan(char32_t cp) {
    return (cp >= 0x3400 && cp <= 0x9FFF) ||   // 扩展A + 统一表意
           (cp >= 0xF900 && cp <= 0xFAFF) ||   // 兼容表意
           (cp >= 0x20000 && cp <= 0x2FA1F);   // 扩展B+
}

std::vector<char> initialsOf(const std::vector<std::string>& alts) {
    std::vector<char> ini;
    for (const std::string& a : alts) {
        if (a.empty()) continue;
        char c = a[0];
        if (std::find(ini.begin(), ini.end(), c) == ini.end()) ini.push_back(c);
    }
    return ini;
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

SearchField buildSearchField(const std::string& utf8, const ReadingLookup& lookup) {
    SearchField field;
    std::size_t i = 0;
    while (i < utf8.size()) {
        char32_t cp = decodeUtf8(utf8, i);
        if (cp < 0x80) {
            unsigned char c = static_cast<unsigned char>(cp);
            if (std::isalnum(c)) {
                std::string a(1, static_cast<char>(std::tolower(c)));
                field.push_back(SearchCell{{a}, {a[0]}});
            }
            // 其它 ASCII（空格/标点）跳过
        } else if (isHan(cp)) {
            std::vector<std::string> readings = lookup(cp);
            std::vector<std::string> alts;
            for (std::string& r : readings) {
                std::string lr = toLower(r);
                if (!lr.empty()) alts.push_back(lr);
            }
            if (!alts.empty()) {
                field.push_back(SearchCell{alts, initialsOf(alts)});
            }
        }
        // 其它非 ASCII（全角标点等）跳过
    }
    return field;
}

}  // namespace openlyrics
