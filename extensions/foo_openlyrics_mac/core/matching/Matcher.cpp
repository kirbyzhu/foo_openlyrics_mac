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

double jaccardSimilarity(const std::string& a, const std::string& b) {
    auto ta = tokenize(a);
    auto tb = tokenize(b);
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
