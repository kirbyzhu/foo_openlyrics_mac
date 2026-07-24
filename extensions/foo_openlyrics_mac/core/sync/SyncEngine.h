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

    // 从当前播放位置按 steps 步进到目标时标行，返回应 seek 到的播放位置（毫秒，>=0）。
    // steps>0 向后（更晚），<0 向前（更早）。目标钳制到 [首个时标行, 末个时标行]。
    // data 非 synced 或无时标行时返回 -1（调用方忽略）。
    static int64_t seekTargetForLineStep(const LyricData& data, int64_t positionMs,
                                         int steps, int64_t extraOffsetMs = 0);
};

}  // namespace openlyrics
