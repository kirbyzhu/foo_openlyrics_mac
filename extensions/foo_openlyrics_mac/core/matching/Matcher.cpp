#include "matching/Matcher.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>
#include <vector>

namespace openlyrics {

namespace {

// 协作标记置换：将 "feat. X" / "ft. X" / "featuring X" / "with X"
// 统一转为 "(feat. X)"，消除不同写法差异。
std::string normalizeCollab(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 10);
    size_t i = 0;
    while (i < s.size()) {
        // 找下一个可能是协作标记的位置（空格后）
        if (i > 0 && s[i-1] == ' ') {
            std::string tail = s.substr(i);
            // 需要精确匹配：标记+空格+内容
            auto startsWithCi = [&](const char* prefix) -> size_t {
                size_t len = 0;
                const char* p = prefix;
                size_t j = 0;
                while (*p && i + j < s.size()) {
                    if (std::tolower(static_cast<unsigned char>(s[i + j])) !=
                        std::tolower(static_cast<unsigned char>(*p)))
                        return 0;
                    ++p; ++j; ++len;
                }
                if (*p) return 0;  // prefix 未匹配完
                return len;
            };
            size_t skip = 0;
            if ((skip = startsWithCi("feat. ")) ||
                (skip = startsWithCi("feat ")) ||
                (skip = startsWithCi("ft. ")) ||
                (skip = startsWithCi("ft ")) ||
                (skip = startsWithCi("featuring "))) {
                result += "(feat. ";
                i += skip;
                // 收集艺术家名直到串尾或遇到 ,/&/(
                while (i < s.size() && s[i] != ',' && s[i] != '&' && s[i] != '(') {
                    result.push_back(s[i]);
                    ++i;
                }
                result.push_back(')');
                continue;
            }
            if ((skip = startsWithCi("with "))) {
                result += "(with ";
                i += skip;
                while (i < s.size() && s[i] != ',' && s[i] != '&' && s[i] != '(') {
                    result.push_back(s[i]);
                    ++i;
                }
                result.push_back(')');
                continue;
            }
        }
        result.push_back(s[i]);
        ++i;
    }
    return result;
}

// 按空格 / - / ( / ) / , 分词
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::string cur;
    for (unsigned char c : s) {
        if (c == ' ' || c == '-' || c == '(' || c == ')' || c == ',' || c == '/') {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(static_cast<char>(c));
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

// UTF-8 字符字节长度（按首字节）。
int utf8CharLen(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;  // 非法首字节按 1 收尾
}

// CJK 感知分词：ASCII 字母数字连续段为一个词 token；
// 非 ASCII 连续段按相邻双字（bigram）生成 token，单字符段用单字。
std::vector<std::string> buildMatchTokens(const std::string& s) {
    std::vector<std::string> tokens;
    std::string asciiWord;
    std::vector<std::string> cjkRun;   // 当前非 ASCII 连续段的字符

    auto flushAscii = [&]() {
        if (!asciiWord.empty()) { tokens.push_back(asciiWord); asciiWord.clear(); }
    };
    auto flushCjk = [&]() {
        if (cjkRun.empty()) return;
        if (cjkRun.size() == 1) {
            tokens.push_back(cjkRun[0]);
        } else {
            for (size_t i = 0; i + 1 < cjkRun.size(); ++i)
                tokens.push_back(cjkRun[i] + cjkRun[i + 1]);
        }
        cjkRun.clear();
    };

    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            flushCjk();
            if (c == ' ' || c == '-' || c == '(' || c == ')' || c == ',' || c == '/') {
                flushAscii();
            } else {
                asciiWord.push_back(static_cast<char>(c));
            }
            ++i;
        } else {
            flushAscii();
            int len = utf8CharLen(c);
            if (i + len > s.size()) len = static_cast<int>(s.size() - i);
            cjkRun.push_back(s.substr(i, len));
            i += len;
        }
    }
    flushAscii();
    flushCjk();
    return tokens;
}

}  // namespace

std::string normalizeForMatch(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c >= 128) {
            // 非 ASCII 字节（UTF-8 多字节序列的一部分，含 CJK）原样保留
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            // 保留一个空格作为分词分隔，但压缩连续空白
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
        } else if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        // 其他 ASCII 字符丢弃
    }
    // 去除首尾空格
    while (!out.empty() && out.back() == ' ') out.pop_back();
    size_t start = 0;
    while (start < out.size() && out[start] == ' ') ++start;
    std::string trimmed = out.substr(start);
    return normalizeCollab(trimmed);
}

std::string normalizeQuery(const std::string& title) {
    // 移除成对括号及内容：ASCII () []、中文全角（）【】
    // 全角字节：（=EF BC 88, ）=EF BC 89, 【=E3 80 90, 】=E3 80 91
    std::string out;
    out.reserve(title.size());
    int depth = 0;
    size_t i = 0;
    while (i < title.size()) {
        unsigned char c = static_cast<unsigned char>(title[i]);
        bool isOpen = (c == '(' || c == '[');
        bool isClose = (c == ')' || c == ']');
        size_t adv = 1;
        if (c == 0xEF && i + 2 < title.size() &&
            (unsigned char)title[i+1] == 0xBC) {
            if ((unsigned char)title[i+2] == 0x88) { isOpen = true; adv = 3; }
            else if ((unsigned char)title[i+2] == 0x89) { isClose = true; adv = 3; }
        } else if (c == 0xE3 && i + 2 < title.size() &&
                   (unsigned char)title[i+1] == 0x80) {
            if ((unsigned char)title[i+2] == 0x90) { isOpen = true; adv = 3; }
            else if ((unsigned char)title[i+2] == 0x91) { isClose = true; adv = 3; }
        }
        if (isOpen) { ++depth; i += adv; continue; }
        if (isClose) { if (depth > 0) --depth; i += adv; continue; }
        if (depth == 0) out.append(title, i, adv);
        i += adv;
    }

    // 移除 feat./ft./featuring 及其后内容（大小写不敏感，词边界在空格后）
    static const char* kFeatMarkers[] = {" feat.", " feat ", " ft.", " ft ", " featuring "};
    std::string lower;
    lower.resize(out.size());
    for (size_t k = 0; k < out.size(); ++k)
        lower[k] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[k])));
    size_t cut = std::string::npos;
    for (const char* m : kFeatMarkers) {
        size_t p = lower.find(m);
        if (p != std::string::npos && p < cut) cut = p;
    }
    if (cut != std::string::npos) out = out.substr(0, cut);

    // 折叠空白 + trim
    std::string collapsed;
    collapsed.reserve(out.size());
    for (char ch : out) {
        if (ch == ' ' || ch == '\t') {
            if (!collapsed.empty() && collapsed.back() != ' ') collapsed.push_back(' ');
        } else {
            collapsed.push_back(ch);
        }
    }
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    size_t start = 0;
    while (start < collapsed.size() && collapsed[start] == ' ') ++start;
    std::string trimmed = collapsed.substr(start);

    return trimmed.empty() ? title : trimmed;
}

double jaccardSimilarity(const std::string& a, const std::string& b) {
    auto ta = buildMatchTokens(a);
    auto tb = buildMatchTokens(b);
    if (ta.empty() && tb.empty()) return 1.0;
    std::set<std::string> setA(ta.begin(), ta.end());
    std::set<std::string> setB(tb.begin(), tb.end());
    size_t intersection = 0;
    for (const auto& w : setA) {
        if (setB.count(w)) ++intersection;
    }
    std::set<std::string> unionSet = setA;
    unionSet.insert(setB.begin(), setB.end());
    if (unionSet.empty()) return 0.0;
    return static_cast<double>(intersection) / unionSet.size();
}

namespace {

int scoreText(const std::string& a, const std::string& b,
              const double jaccardThresholds[3]) {
    if (a.empty() || b.empty()) return 0;
    std::string na = normalizeForMatch(a);
    std::string nb = normalizeForMatch(b);
    if (na.empty() || nb.empty()) return 0;
    if (na == nb) return 100;
    if (na.find(nb) != std::string::npos || nb.find(na) != std::string::npos) return 90;
    double j = jaccardSimilarity(na, nb);
    if (j >= jaccardThresholds[0]) return 80;
    if (j >= jaccardThresholds[1]) return 60;
    if (j >= jaccardThresholds[2]) return 30;
    return 0;
}

}  // namespace

Matcher::Matcher(const MatchWeights& w) : w_(w) {}

int Matcher::score(const TrackMeta& track, const SearchResult& candidate) const {
    int ts = scoreTitle(track.title, candidate.trackName);
    int as = scoreArtist(track.artist, candidate.artistName);
    int al = scoreAlbum(track.album, candidate.albumName);
    int ds = scoreDuration(track.lengthMs, candidate.durationSec);
    return static_cast<int>(std::round(ts * w_.title + as * w_.artist +
                                        al * w_.album + ds * w_.duration));
}

bool Matcher::isHighConfidence(int s) const { return s >= kHighThreshold; }
bool Matcher::isLowConfidence(int s) const { return s >= kLowThreshold && s < kHighThreshold; }

int Matcher::scoreTitle(const std::string& a, const std::string& b) const {
    static const double kTitleJaccardThresholds[3] = {0.75, 0.5, 0.25};
    return scoreText(a, b, kTitleJaccardThresholds);
}

int Matcher::scoreArtist(const std::string& a, const std::string& b) const {
    static const double kArtistJaccardThresholds[3] = {0.8, 0.6, 0.3};
    return scoreText(a, b, kArtistJaccardThresholds);
}

int Matcher::scoreAlbum(const std::string& a, const std::string& b) const {
    if (a.empty() || b.empty()) return 0;
    std::string na = normalizeForMatch(a);
    std::string nb = normalizeForMatch(b);
    if (na.empty() || nb.empty()) return 0;
    if (na.find(nb) != std::string::npos || nb.find(na) != std::string::npos) return 100;
    if (jaccardSimilarity(na, nb) >= 0.5) return 60;
    return 0;
}

int Matcher::scoreDuration(int64_t trackMs, int candidateSec) const {
    if (candidateSec <= 0 || trackMs <= 0) return 0;
    int64_t candidateMs = static_cast<int64_t>(candidateSec) * 1000;
    int64_t diff = std::abs(trackMs - candidateMs);
    if (diff <= 3000) return 100;
    if (diff <= 8000) return 70;
    if (diff <= 15000) return 40;
    return 0;
}

}  // namespace openlyrics
