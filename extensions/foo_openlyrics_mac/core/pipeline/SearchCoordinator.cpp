#include "pipeline/SearchCoordinator.h"
#include <algorithm>
#include <map>

namespace openlyrics {

SearchCoordinator::SearchCoordinator(SearchPipeline* localPipeline,
                                     std::vector<LyricSource*> onlineSources,
                                     Matcher& matcher)
    : localPipeline_(localPipeline)
    , onlineSources_(std::move(onlineSources))
    , matcher_(matcher) {}

SearchCoordinator::SearchCoordinator(std::vector<LyricSource*> onlineSources,
                                     Matcher& matcher)
    : localPipeline_(nullptr)
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
    if (localPipeline_ && localPipeline_->resolve(track, out)) return true;

    // 2. 在线候选池评分
    auto pool = collectAndScore(track);
    if (pool.empty()) return false;

    // 3. 取最高分判断是否达到最低阈值
    if (pool[0].score < kLowThreshold) return false;

    // 4. 按分数降序遍历候选，找到第一个能成功拉取的
    for (const auto& candidate : pool) {
        if (candidate.score < kLowThreshold) break;
        for (auto* source : onlineSources_) {
            if (source && source->sourceId() == candidate.source) {
                if (source->fetchById(candidate.id, out)) return true;
                break;  // 该 source 拉取失败，尝试下一个候选
            }
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
