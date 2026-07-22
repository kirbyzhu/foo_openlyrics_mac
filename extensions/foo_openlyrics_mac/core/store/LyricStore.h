#pragma once
#include "ports/FileSystem.h"
#include "model/TrackMeta.h"
#include "model/LyricData.h"

namespace openlyrics {

class LyricStore {
public:
    explicit LyricStore(FileSystem& fs);
    // 把 data.sourceText 无损写到 <track.path 去扩展名>.lrc。
    // sourceText 为空 → 不写，返回 false。否则返回 writeFile 结果。
    bool save(const TrackMeta& track, const LyricData& data);

private:
    FileSystem& fs_;
};

}  // namespace openlyrics
