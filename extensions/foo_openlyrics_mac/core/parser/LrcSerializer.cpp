#include "parser/LrcSerializer.h"
#include <cstdint>
#include <cstdio>

namespace openlyrics {

std::string LrcSerializer::serialize(const LyricData& data) {
    std::string out;
    for (const auto& t : data.tags) {
        out += "[" + t.first + ":" + t.second + "]\n";
    }
    if (data.offsetMs != 0) {
        out += "[offset:" + std::to_string(data.offsetMs) + "]\n";
    }
    auto appendTag = [&out](int64_t total) {
        int64_t minutes = total / 60000;
        int64_t seconds = (total % 60000) / 1000;
        int64_t hundredths = (total % 1000) / 10;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "[%02lld:%02lld.%02lld]",
                      (long long)minutes, (long long)seconds, (long long)hundredths);
        out += buf;
    };
    for (const auto& line : data.lines) {
        bool lineTagEmitted = (line.timeMs >= 0);
        if (lineTagEmitted) appendTag(line.timeMs);
        if (line.syllables.size() > 1) {
            for (size_t i = 0; i < line.syllables.size(); ++i) {
                const auto& syl = line.syllables[i];
                // 行级已发时标时首音节沿用之（避免重复）；行级无时标时首音节补发自身时标，
                // 否则首音节起始时刻丢失、无法回环。
                bool needTag = syl.startMs >= 0 && (i > 0 || !lineTagEmitted);
                if (needTag) appendTag(syl.startMs);
                out += syl.text;
            }
        } else {
            out += line.text;
        }
        out += "\n";
    }
    return out;
}

}  // namespace openlyrics
