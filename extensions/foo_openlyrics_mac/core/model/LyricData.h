#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace openlyrics {

struct Syllable {          // word-level 音节
    int64_t startMs = 0;
    int64_t endMs = 0;     // syllable 结束时标；0 表示未知
    std::string text;
};

struct LyricLine {
    int64_t timeMs = -1;               // 行起始时标，毫秒；无时标行为 -1
    std::string text;
    std::vector<Syllable> syllables;   // word-level 音节列表
};

struct LyricData {
    std::vector<LyricLine> lines;                              // synced 行按 timeMs 升序
    std::vector<std::pair<std::string, std::string>> tags;    // ID 标签，如 ti/ar/al/by
    int64_t offsetMs = 0;                                     // [offset:] 值，毫秒
    bool synced = false;                                      // 是否含有效时标行
    std::string sourceText;                                   // 原始输入文本，用于无损存储

    bool hasSyllables() const {
        for (const auto& l : lines) {
            if (l.syllables.size() > 1) return true;
        }
        return false;
    }
};

}  // namespace openlyrics
