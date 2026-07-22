#pragma once
#include "model/LyricData.h"
#include "model/TrackMeta.h"

namespace openlyrics {

class LyricSource {
public:
    virtual ~LyricSource() = default;
    // 命中返回 true 并填 out；未命中返回 false。
    virtual bool fetch(const TrackMeta& track, LyricData& out) = 0;
};

}  // namespace openlyrics
