#include "sync/SyncEngine.h"

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

}  // namespace openlyrics
