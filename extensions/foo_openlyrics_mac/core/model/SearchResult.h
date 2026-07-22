#pragma once
#include <string>

namespace openlyrics {

struct SearchResult {
    int id = 0;
    std::string trackName;
    std::string artistName;
    std::string albumName;
    int durationSec = 0;  // 秒
};

}  // namespace openlyrics
