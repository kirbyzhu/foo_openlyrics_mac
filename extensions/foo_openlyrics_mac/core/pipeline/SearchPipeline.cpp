#include "pipeline/SearchPipeline.h"
#include <utility>

namespace openlyrics {

SearchPipeline::SearchPipeline(std::vector<LyricSource*> sources)
    : sources_(std::move(sources)) {}

bool SearchPipeline::resolve(const TrackMeta& track, LyricData& out, CancelToken* cancel) {
    for (LyricSource* source : sources_) {
        if (cancel && cancel->isCancelled()) return false;
        if (source && source->fetch(track, out, cancel)) return true;
    }
    return false;
}

}  // namespace openlyrics
