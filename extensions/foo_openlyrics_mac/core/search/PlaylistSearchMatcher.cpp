#include "search/PlaylistSearchMatcher.h"

#include <algorithm>

namespace openlyrics {
namespace {

// 全拼模式：q 能否顺序消费 field（每 cell 跳过或消费其某个 alternative）。
bool fullModeExists(const SearchField& field, const std::string& q) {
    const std::size_t M = q.size();
    if (M == 0) return true;
    std::vector<char> reach(M + 1, 0);
    reach[0] = 1;
    for (const SearchCell& cell : field) {
        for (int j = static_cast<int>(M); j >= 0; --j) {
            if (!reach[j]) continue;
            for (const std::string& alt : cell.alternatives) {
                std::size_t L = alt.size();
                if (L > 0 && static_cast<std::size_t>(j) + L <= M &&
                    q.compare(static_cast<std::size_t>(j), L, alt) == 0) {
                    reach[j + static_cast<int>(L)] = 1;
                }
            }
        }
    }
    return reach[M] != 0;
}

// 首字母模式：q 每字符按序匹配某 cell 的一个 initial（每 cell 至多贡献一字符）。
bool initialsExists(const SearchField& field, const std::string& q) {
    if (q.empty()) return true;
    std::size_t j = 0;
    for (const SearchCell& cell : field) {
        for (char ini : cell.initials) {
            if (ini == q[j]) { ++j; break; }
        }
        if (j == q.size()) return true;
    }
    return false;
}

std::string primaryFull(const SearchField& field) {
    std::string s;
    for (const SearchCell& c : field)
        if (!c.alternatives.empty()) s += c.alternatives[0];
    return s;
}

std::string primaryInitials(const SearchField& field) {
    std::string s;
    for (const SearchCell& c : field)
        if (!c.initials.empty()) s += c.initials[0];
    return s;
}

}  // namespace

int scoreField(const SearchField& field, const std::string& query) {
    if (query.empty()) return 0;
    int score = -1;
    if (fullModeExists(field, query)) {
        int s = 600;
        if (primaryFull(field).find(query) != std::string::npos) s += 200;
        score = std::max(score, s);
    }
    if (initialsExists(field, query)) {
        int s = 300;
        if (primaryInitials(field).find(query) != std::string::npos) s += 100;
        score = std::max(score, s);
    }
    return score;
}

std::vector<MatchHit> matchPlaylist(const std::vector<SearchRecord>& records,
                                    const std::string& query) {
    std::vector<MatchHit> hits;
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (query.empty()) {
            hits.push_back({i, 0});
            continue;
        }
        const SearchRecord& r = records[i];
        int st = scoreField(r.title, query);
        int sa = scoreField(r.artist, query);
        int sb = scoreField(r.album, query);
        int best = -1;
        if (st >= 0) best = std::max(best, st + 50);
        if (sa >= 0) best = std::max(best, sa + 20);
        if (sb >= 0) best = std::max(best, sb + 0);
        if (best >= 0) hits.push_back({i, best});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const MatchHit& a, const MatchHit& b) { return a.score > b.score; });
    return hits;
}

}  // namespace openlyrics
