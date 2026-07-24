#pragma once
#include "sources/LyricSource.h"
#include "ports/CancelToken.h"
#include <vector>

namespace openlyrics {

// 按序尝试一组歌词源，命中即短路返回
class SearchPipeline {
public:
    explicit SearchPipeline(std::vector<LyricSource*> sources);
    bool resolve(const TrackMeta& track, LyricData& out, CancelToken* cancel = nullptr);

private:
    std::vector<LyricSource*> sources_;
};

}  // namespace openlyrics
