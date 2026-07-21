#pragma once
#include "model/LyricData.h"
#include <string>

namespace openlyrics {

class LrcParser {
public:
    // 解析 LRC 或纯文本。含任一时标行则 synced=true，
    // 时标行按 timeMs 升序；纯文本行 timeMs=-1 并保留原始顺序。
    static LyricData parse(const std::string& text);
};

}  // namespace openlyrics
