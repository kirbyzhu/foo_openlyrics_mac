#pragma once
#include "model/LyricData.h"
#include <cstdint>

namespace openlyrics {

struct SyncResult {
    int lineIndex = -1;     // -1 表示尚未到达首个时标行
    double progress = 0.0;  // 当前行到下一行的插值进度 [0,1)，末行为 0
};

class SyncEngine {
public:
    // data.lines 需为按 timeMs 升序的行（LrcParser 已保证）。
    // 无时标数据（synced=false）恒返回 {-1, 0}。
    static SyncResult locate(const LyricData& data, int64_t positionMs,
                             int64_t extraOffsetMs = 0);
};

}  // namespace openlyrics
