#pragma once
#include "model/TrackMeta.h"
#include <string>

namespace openlyrics {

class TagIO {
public:
    virtual ~TagIO() = default;
    // 读内嵌歌词标签，成功写 out 并返回 true
    virtual bool readLyricTag(const TrackMeta& track, std::string& out) = 0;
    // 写回内嵌歌词标签
    virtual bool writeLyricTag(const TrackMeta& track, const std::string& lrc) = 0;
};

}  // namespace openlyrics
