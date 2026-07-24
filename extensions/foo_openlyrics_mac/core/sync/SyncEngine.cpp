#include "sync/SyncEngine.h"
#include <vector>

namespace openlyrics {

SyncResult SyncEngine::locate(const LyricData& data, int64_t positionMs,
                              int64_t extraOffsetMs) {
    SyncResult result;
    if (!data.synced) return result;

    const int64_t eff = positionMs + data.offsetMs + extraOffsetMs;

    // 找满足 timeMs <= eff 的最后一个时标行
    int current = -1;
    for (int i = 0; i < (int)data.lines.size(); ++i) {
        if (data.lines[i].timeMs < 0) continue;      // 跳过无时标行
        if (data.lines[i].timeMs <= eff) {
            current = i;
        } else {
            break;                                   // 已升序，后面更大
        }
    }
    result.lineIndex = current;
    if (current < 0) return result;

    // 找下一个有时标的行以算插值
    int next = -1;
    for (int j = current + 1; j < (int)data.lines.size(); ++j) {
        if (data.lines[j].timeMs >= 0) { next = j; break; }
    }
    if (next >= 0) {
        int64_t start = data.lines[current].timeMs;
        int64_t end = data.lines[next].timeMs;
        if (end > start) {
            double p = double(eff - start) / double(end - start);
            if (p < 0.0) p = 0.0;
            if (p >= 1.0) p = 0.999999;
            result.progress = p;
        }
    }
    return result;
}

int64_t SyncEngine::seekTargetForLineStep(const LyricData& data, int64_t positionMs,
                                          int steps, int64_t extraOffsetMs) {
    if (!data.synced) return -1;

    // 收集有时标行的下标
    std::vector<int> timed;
    for (int i = 0; i < (int)data.lines.size(); ++i) {
        if (data.lines[i].timeMs >= 0) timed.push_back(i);
    }
    if (timed.empty()) return -1;

    // 当前行（data.lines 下标；-1 表示尚未到首句）
    int cur = locate(data, positionMs, extraOffsetMs).lineIndex;

    // 当前行在 timed 中的序号；cur==-1 记为 -1
    int ordinal = -1;
    for (int k = 0; k < (int)timed.size(); ++k) {
        if (timed[k] == cur) { ordinal = k; break; }
    }

    int targetOrd = ordinal + steps;
    if (targetOrd < 0) targetOrd = 0;
    if (targetOrd > (int)timed.size() - 1) targetOrd = (int)timed.size() - 1;

    int64_t targetTimeMs = data.lines[timed[targetOrd]].timeMs;
    int64_t seekMs = targetTimeMs - data.offsetMs - extraOffsetMs;
    if (seekMs < 0) seekMs = 0;
    return seekMs;
}

}  // namespace openlyrics
