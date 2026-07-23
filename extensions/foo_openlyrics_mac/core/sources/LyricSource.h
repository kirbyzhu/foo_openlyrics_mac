#pragma once
#include "model/LyricData.h"
#include "model/TrackMeta.h"
#include "model/SearchResult.h"
#include <vector>

namespace openlyrics {

class LyricSource {
public:
    virtual ~LyricSource() = default;

    // 搜索候选列表。默认返回 false。
    virtual bool search(const TrackMeta& track, std::vector<SearchResult>& out) {
        (void)track; (void)out;
        return false;
    }

    // 按 ID 拉取歌词。默认返回 false。
    virtual bool fetchById(const std::string& id, LyricData& out) {
        (void)id; (void)out;
        return false;
    }

    // 自动模式一键取词。默认实现：search 取第一候选 → fetchById。
    // TagSource / LocalFileSource 覆写此方法提供自己的取词逻辑。
    virtual bool fetch(const TrackMeta& track, LyricData& out) {
        std::vector<SearchResult> results;
        if (!search(track, results) || results.empty()) return false;
        return fetchById(results[0].id, out);
    }

    // 返回此源的类型标识。默认 Unknown。
    virtual SourceId sourceId() const { return SourceId::Unknown; }
};

}  // namespace openlyrics
