#include "parser/LrcSerializer.h"
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
    for (const auto& line : data.lines) {
        if (line.timeMs >= 0) {
            int64_t total = line.timeMs;
            int64_t minutes = total / 60000;
            int64_t seconds = (total % 60000) / 1000;
            int64_t hundredths = (total % 1000) / 10;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "[%02lld:%02lld.%02lld]",
                          (long long)minutes, (long long)seconds, (long long)hundredths);
            out += buf;
        }
        out += line.text;
        out += "\n";
    }
    return out;
}

}  // namespace openlyrics
