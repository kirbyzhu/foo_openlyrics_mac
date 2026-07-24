#include "pipeline/SearchCoordinator.h"
#include <algorithm>
#include <map>

namespace openlyrics {

namespace {
// 源质量偏好：同分或接近时优先歌词更干净的源。LrcLib/QQ 的同步歌词
// 通常比 NetEase 干净（NetEase 常混入开场对白、yrc 元数据行）。
// 加成仅用于排序，不改 r.score，故不影响置信阈值判定。
int sourceRankBonus(SourceId s) {
    switch (s) {
        case SourceId::LrcLib:  return 6;
        case SourceId::QQMusic: return 5;
        default:                return 0;  // NetEase 及其他不加成
    }
}
}  // namespace

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

std::vector<SearchResult> SearchCoordinator::collectAndScore(const TrackMeta& track, CancelToken* cancel) {
    std::vector<SearchResult> pool;
    for (auto* source : onlineSources_) {
        if (cancel && cancel->isCancelled()) break;
        if (!source) continue;
        std::vector<SearchResult> results;
        if (source->search(track, results, cancel)) {
            for (auto& r : results) {
                r.source = source->sourceId();
                r.score = matcher_.score(track, r);
                pool.push_back(std::move(r));
            }
        }
    }
    std::sort(pool.begin(), pool.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  int aa = a.score + sourceRankBonus(a.source);
                  int bb = b.score + sourceRankBonus(b.source);
                  if (aa != bb) return aa > bb;
                  return a.score > b.score;
              });
    return pool;
}

bool SearchCoordinator::resolve(const TrackMeta& track, LyricData& out, CancelToken* cancel) {
    if (cancel && cancel->isCancelled()) return false;

    // 1. 本地快速通道
    if (localPipeline_ && localPipeline_->resolve(track, out, cancel)) return true;
    if (cancel && cancel->isCancelled()) return false;

    // 2. 在线候选池评分
    auto pool = collectAndScore(track, cancel);
    if (pool.empty() || (cancel && cancel->isCancelled())) return false;

    // 3. 取最高分判断是否达到最低阈值
    if (pool[0].score < kLowThreshold) return false;

    // 4. 按分数降序遍历候选，找到第一个能成功拉取的
    for (const auto& candidate : pool) {
        if (cancel && cancel->isCancelled()) return false;
        if (candidate.score < kLowThreshold) break;
        for (auto* source : onlineSources_) {
            if (cancel && cancel->isCancelled()) return false;
            if (source && source->sourceId() == candidate.source) {
                // 优先 ID 查询
                if (source->fetchById(candidate.id, out, cancel)) return true;
                if (cancel && cancel->isCancelled()) return false;
                // ID 查询失败 → 用候选元数据构造 TrackMeta 做命名查询回退
                // LrcLibProvider::fetch() 走 /api/get?artist_name=&track_name= 路径，
                // 不依赖 fetchById，可在 ID 端点不可用时成功拉取。
                if (!candidate.trackName.empty()) {
                    TrackMeta fallback;
                    fallback.title = candidate.trackName;
                    fallback.artist = candidate.artistName;
                    fallback.album = candidate.albumName;
                    fallback.lengthMs = static_cast<int64_t>(candidate.durationSec) * 1000;
                    if (source->fetch(fallback, out, cancel)) return true;
                }
                break;
            }
        }
    }
    return false;
}

std::vector<GroupedResults> SearchCoordinator::searchAll(const TrackMeta& track, CancelToken* cancel) {
    auto pool = collectAndScore(track, cancel);

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
