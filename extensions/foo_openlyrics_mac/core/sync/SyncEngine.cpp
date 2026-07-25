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

    // syllable 级定位
    if (current >= 0) {
        const auto& syls = data.lines[current].syllables;
        if (syls.size() > 1) {
            int sylIdx = -1;
            for (int s = 0; s < (int)syls.size(); ++s) {
                if (syls[s].startMs <= eff) sylIdx = s;
                else break;
            }
            if (sylIdx >= 0) {
                result.syllableIndex = sylIdx;
                int64_t sylStart = syls[sylIdx].startMs;
                int64_t sylEnd = syls[sylIdx].endMs;
                if (sylEnd <= sylStart && sylIdx + 1 < (int)syls.size())
                    sylEnd = syls[sylIdx + 1].startMs;
                if (sylEnd > sylStart) {
                    double sp = double(eff - sylStart) / double(sylEnd - sylStart);
                    if (sp < 0.0) sp = 0.0;
                    if (sp >= 1.0) sp = 0.999999;
                    result.syllableProgress = sp;
                }
            }
        }
    }

    return result;
}

int SyncEngine::adjacentTimedLine(const LyricData& data, int fromLine, int dir) {
    const int n = (int)data.lines.size();
    if (dir > 0) {
        for (int i = fromLine + 1; i < n; ++i)
            if (data.lines[i].timeMs >= 0) return i;
    } else {
        for (int i = fromLine - 1; i >= 0; --i)
            if (data.lines[i].timeMs >= 0) return i;
    }
    return fromLine >= 0 ? fromLine : -1;   // 端点停住；fromLine==-1 无可达则 -1
}

}  // namespace openlyrics
