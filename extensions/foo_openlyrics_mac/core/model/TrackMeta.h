#pragma once
#include <cstdint>
#include <string>

namespace openlyrics {

struct TrackMeta {
    std::string artist;
    std::string title;
    std::string album;
    std::string path;
    int64_t lengthMs = 0;
};

}  // namespace openlyrics
