#include "store/LyricStore.h"

#include <algorithm>
#include <cctype>

namespace openlyrics {

namespace {

std::string stripExtension(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return path.substr(0, dot);
    }
    return path;
}

std::string DirOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);
}

std::string BasenameOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

std::string ToLower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool CiEquals(const std::string& a, const std::string& b) {
    return ToLower(a) == ToLower(b);
}

std::string lrcTarget(const TrackMeta& track) {
    return stripExtension(track.path) + ".lrc";
}

}  // namespace

LyricStore::LyricStore(FileSystem& fs) : fs_(fs) {}

bool LyricStore::save(const TrackMeta& track, const LyricData& data) {
    if (data.sourceText.empty()) {
        return false;
    }

    const std::string target = lrcTarget(track);
    const std::string dir = DirOf(target);
    const std::string targetName = BasenameOf(target);

    for (const std::string& entry : fs_.listDirectory(dir)) {
        if (CiEquals(entry, targetName)) {
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
