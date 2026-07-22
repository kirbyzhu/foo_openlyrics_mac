#include "sources/TagSource.h"

namespace openlyrics {

TagSource::TagSource(TagIO& tagio) : tagio_(tagio) {}

bool TagSource::fetch(const TrackMeta& track, LyricData& out) {
    std::string text;
    if (!tagio_.readLyricTag(track, text)) return false;
    if (text.empty()) return false;
    out = LrcParser::parse(text);
    return true;
}

}  // namespace openlyrics
