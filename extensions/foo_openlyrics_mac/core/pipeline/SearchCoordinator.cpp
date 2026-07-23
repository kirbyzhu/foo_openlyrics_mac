#include "pipeline/SearchCoordinator.h"
#include <algorithm>
#include <map>
#include <cstdio>

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
            fprintf(stderr, "[SearchCoordinator] source=%s search hits=%zu\n",
                    sourceDisplayName(source->sourceId()), results.size());
            for (auto& r : results) {
                r.source = source->sourceId();
                r.score = matcher_.score(track, r);
                fprintf(stderr, "  id=%s title='%s' artist='%s' dur=%d score=%d\n",
                        r.id.c_str(), r.trackName.c_str(), r.artistName.c_str(),
                        r.durationSec, r.score);
                pool.push_back(std::move(r));
            }
        } else {
            fprintf(stderr, "[SearchCoordinator] source=%s search returned empty\n",
                    sourceDisplayName(source->sourceId()));
        }
    }
    std::sort(pool.begin(), pool.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.score > b.score;
              });
    fprintf(stderr, "[SearchCoordinator] pool total=%zu top_score=%d\n",
            pool.size(), pool.empty() ? -1 : pool[0].score);
    return pool;
}

bool SearchCoordinator::resolve(const TrackMeta& track, LyricData& out) {
    fprintf(stderr, "[SearchCoordinator] resolve artist='%s' title='%s' lenMs=%lld\n",
            track.artist.c_str(), track.title.c_str(), (long long)track.lengthMs);

    // 1. 本地快速通道
    if (localPipeline_ && localPipeline_->resolve(track, out)) {
        fprintf(stderr, "[SearchCoordinator] local pipeline hit\n");
        return true;
    }

    // 2. 在线候选池评分
    auto pool = collectAndScore(track);
    if (pool.empty()) {
        fprintf(stderr, "[SearchCoordinator] pool empty, returning false\n");
        return false;
    }

    // 3. 取最高分判断是否达到最低阈值
    if (pool[0].score < kLowThreshold) {
        fprintf(stderr, "[SearchCoordinator] top score %d < threshold %d, returning false\n",
                pool[0].score, kLowThreshold);
        return false;
    }

    // 4. 按分数降序遍历候选，找到第一个能成功拉取的
    for (const auto& candidate : pool) {
        if (candidate.score < kLowThreshold) break;
        for (auto* source : onlineSources_) {
            if (source && source->sourceId() == candidate.source) {
                bool ok = source->fetchById(candidate.id, out);
                fprintf(stderr, "[SearchCoordinator] fetchById src=%s id=%s score=%d → %s\n",
                        sourceDisplayName(candidate.source), candidate.id.c_str(),
                        candidate.score, ok ? "OK" : "FAIL");
                if (ok) return true;
                break;
            }
        }
    }
    fprintf(stderr, "[SearchCoordinator] all fetchById failed\n");
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
