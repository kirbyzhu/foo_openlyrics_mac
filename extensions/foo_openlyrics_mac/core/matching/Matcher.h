#pragma once
#include "model/TrackMeta.h"
#include "model/SearchResult.h"
#include <cstdint>
#include <string>

namespace openlyrics {

struct MatchWeights {
    float title    = 0.40f;
    float artist   = 0.25f;
    float album    = 0.15f;
    float duration = 0.20f;
};

// 文本归一化：小写 + 去标点空白 + 全角转半角 + 协作标记置换。
// 公开以便 SearchCoordinator 和测试复用。
std::string normalizeForMatch(const std::string& s);

// 分词 + Jaccard 系数。公开以便测试。
double jaccardSimilarity(const std::string& a, const std::string& b);

class Matcher {
public:
    explicit Matcher(const MatchWeights& w = {});
    int score(const TrackMeta& track, const SearchResult& candidate) const;
    bool isHighConfidence(int s) const;  // s >= 70
    bool isLowConfidence(int s) const;   // 40 <= s < 70

private:
    int scoreTitle(const std::string& a, const std::string& b) const;
    int scoreArtist(const std::string& a, const std::string& b) const;
    int scoreAlbum(const std::string& a, const std::string& b) const;
    int scoreDuration(int64_t trackMs, int candidateSec) const;

    MatchWeights w_;
    static constexpr int kHighThreshold = 70;
    static constexpr int kLowThreshold  = 40;
};

}  // namespace openlyrics
