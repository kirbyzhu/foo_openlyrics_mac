#include "pipeline/SearchPipeline.h"
#include <utility>

namespace openlyrics {

SearchPipeline::SearchPipeline(std::vector<LyricSource*> sources)
    : sources_(std::move(sources)) {}

bool SearchPipeline::resolve(const TrackMeta& track, LyricData& out) {
    for (LyricSource* source : sources_) {
        if (source && source->fetch(track, out)) return true;
    }
    return false;
}

}  // namespace openlyrics
