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

    // 从 fromLine（data.lines 下标，-1=尚未到首句）沿 dir(+1 前进/-1 后退) 找相邻的有时标行下标。
    // 跳过 timeMs<0 的行。端点无更多时标行时返回 fromLine（停住）；fromLine==-1 且无可达时标行返回 -1。
    static int adjacentTimedLine(const LyricData& data, int fromLine, int dir);
};

}  // namespace openlyrics
