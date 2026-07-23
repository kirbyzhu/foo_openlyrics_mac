#pragma once
#include "sources/LyricSource.h"
#include "matching/Matcher.h"
#include "pipeline/SearchPipeline.h"
#include "model/LyricData.h"
#include "model/TrackMeta.h"
#include "model/SearchResult.h"
#include <vector>

namespace openlyrics {

struct GroupedResults {
    SourceId source;
    std::string sourceName;
    std::vector<SearchResult> items;  // 已含分数，降序
};

class SearchCoordinator {
public:
    // 带本地快速通道的构造器（自动模式）
    SearchCoordinator(SearchPipeline* localPipeline,
                      std::vector<LyricSource*> onlineSources,
                      Matcher& matcher);

    // 仅在线源的构造器（手动搜索模式）
    SearchCoordinator(std::vector<LyricSource*> onlineSources, Matcher& matcher);

    // 自动模式：本地快速通道 → 在线候选池评分 → 取最优
    bool resolve(const TrackMeta& track, LyricData& out);

    // 手动模式：在线候选池评分 → 按源分组返回
    std::vector<GroupedResults> searchAll(const TrackMeta& track);

private:
    // 并行调用所有在线源 search()，收集候选池并评分降序
    std::vector<SearchResult> collectAndScore(const TrackMeta& track);

    SearchPipeline* localPipeline_;  // 可为 nullptr
    std::vector<LyricSource*> onlineSources_;
    Matcher& matcher_;
    static constexpr int kLowThreshold = 40;
};

}  // namespace openlyrics
