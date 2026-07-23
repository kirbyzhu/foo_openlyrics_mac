#include "pipeline/SearchCoordinator.h"
#include <algorithm>
#include <map>

namespace openlyrics {

SearchCoordinator::SearchCoordinator(SearchPipeline& localPipeline,
                                     std::vector<LyricSource*> onlineSources,
                                     Matcher& matcher)
    : localPipeline_(localPipeline)
    , onlineSources_(std::move(onlineSources))
    , matcher_(matcher) {}

std::vector<SearchResult> SearchCoordinator::collectAndScore(const TrackMeta& track) {
    std::vector<SearchResult> pool;
    for (auto* source : onlineSources_) {
        if (!source) continue;
        std::vector<SearchResult> results;
        if (source->search(track, results)) {
            for (auto& r : results) {
                r.source = source->sourceId();
                r.score = matcher_.score(track, r);
                pool.push_back(std::move(r));
            }
        }
    }
    std::sort(pool.begin(), pool.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.score > b.score;
              });
    return pool;
}

bool SearchCoordinator::resolve(const TrackMeta& track, LyricData& out) {
    // 1. 本地快速通道
    if (localPipeline_.resolve(track, out)) return true;

    // 2. 在线候选池评分
    auto pool = collectAndScore(track);
    if (pool.empty()) return false;

    // 3. 最优候选
    const auto& best = pool[0];
    if (best.score < kLowThreshold) return false;

    // 4. 按 source 找到对应 provider 拉取
    for (auto* source : onlineSources_) {
        if (source && source->sourceId() == best.source) {
            return source->fetchById(best.id, out);
        }
    }
    return false;
}

std::vector<GroupedResults> SearchCoordinator::searchAll(const TrackMeta& track) {
    auto pool = collectAndScore(track);

    // 按 sourceId 分组
    std::map<SourceId, std::vector<SearchResult>> groups;
    for (auto& r : pool) {
        groups[r.source].push_back(std::move(r));
    }

    std::vector<GroupedResults> result;
    for (auto& [sid, items] : groups) {
        result.push_back({sid, sourceDisplayName(sid), std::move(items)});
    }
    return result;
}

}  // namespace openlyrics
