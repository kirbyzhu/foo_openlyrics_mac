#include "store/LyricStore.h"
#include "internal/PathUtils.h"
#include "sources/LocalFileSource.h"

#include <cctype>

namespace openlyrics {

using internal::dirOf;
using internal::basenameOf;
using internal::ciEquals;

namespace {

std::string lrcTarget(const TrackMeta& track) {
    return LocalFileSource::stripExtension(track.path) + ".lrc";
}

}  // namespace

LyricStore::LyricStore(FileSystem& fs) : fs_(fs) {}

bool LyricStore::save(const TrackMeta& track, const LyricData& data) {
    if (data.sourceText.empty()) {
        return false;
    }

    const std::string target = lrcTarget(track);
    const std::string dir = dirOf(target);
    const std::string targetName = basenameOf(target);

    for (const std::string& entry : fs_.listDirectory(dir)) {
        if (ciEquals(entry, targetName)) {
            return false;
        }
    }

    return fs_.writeFile(target, data.sourceText);
}

bool LyricStore::forceSave(const TrackMeta& track, const LyricData& data) {
    if (data.sourceText.empty()) {
        return false;
    }
    return fs_.writeFile(lrcTarget(track), data.sourceText);
}

}  // namespace openlyrics
