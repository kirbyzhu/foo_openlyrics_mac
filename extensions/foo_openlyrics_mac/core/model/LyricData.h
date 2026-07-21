#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace openlyrics {

struct Syllable {          // 预留 word-level，首版不填充
    int64_t startMs = 0;
    std::string text;
};

struct LyricLine {
    int64_t timeMs = -1;               // 行起始时标，毫秒；无时标行为 -1
    std::string text;
    std::vector<Syllable> syllables;   // 预留，首版为空
};

struct LyricData {
    std::vector<LyricLine> lines;                              // synced 行按 timeMs 升序
    std::vector<std::pair<std::string, std::string>> tags;    // ID 标签，如 ti/ar/al/by
    int64_t offsetMs = 0;                                     // [offset:] 值，毫秒
    bool synced = false;                                      // 是否含有效时标行
};

}  // namespace openlyrics
