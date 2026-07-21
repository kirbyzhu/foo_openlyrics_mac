#pragma once
#include "model/LyricData.h"
#include <string>

namespace openlyrics {

class LrcSerializer {
public:
    // 输出标准 LRC。先写 id 标签，再写 offset（非 0 时），
    // 再按顺序写 [mm:ss.xx]text；无时标行仅写文本。
    static std::string serialize(const LyricData& data);
};

}  // namespace openlyrics
