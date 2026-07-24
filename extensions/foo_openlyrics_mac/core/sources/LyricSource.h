#pragma once
#include "model/LyricData.h"
#include "model/TrackMeta.h"
#include "model/SearchResult.h"
#include "ports/CancelToken.h"
#include <vector>

namespace openlyrics {

class LyricSource {
public:
    virtual ~LyricSource() = default;

    // 搜索候选列表。默认返回 false。
    virtual bool search(const TrackMeta& track, std::vector<SearchResult>& out,
                        CancelToken* cancel = nullptr) {
        (void)track; (void)out; (void)cancel;
        return false;
    }

    // 按 ID 拉取歌词。默认返回 false。
    virtual bool fetchById(const std::string& id, LyricData& out,
                           CancelToken* cancel = nullptr) {
        (void)id; (void)out; (void)cancel;
        return false;
    }

    // 自动模式一键取词。默认实现：search 取第一候选 → fetchById。
    // TagSource / LocalFileSource 覆写此方法提供自己的取词逻辑。
    virtual bool fetch(const TrackMeta& track, LyricData& out,
                       CancelToken* cancel = nullptr) {
        std::vector<SearchResult> results;
        if (!search(track, results, cancel) || results.empty()) return false;
        if (cancel && cancel->isCancelled()) return false;
        return fetchById(results[0].id, out, cancel);
    }

    // 返回此源的类型标识。默认 Unknown。
    virtual SourceId sourceId() const { return SourceId::Unknown; }
};

}  // namespace openlyrics
