#include "store/LyricStore.h"

namespace openlyrics {

namespace {

std::string stripExtension(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const size_t dot = path.find_last_of('.');
    // 只有当 '.' 落在最后一个路径分隔符之后（即属于文件名本身），才视为扩展名分隔符
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return path.substr(0, dot);
    }
    return path;
}

}  // namespace

LyricStore::LyricStore(FileSystem& fs) : fs_(fs) {}

bool LyricStore::save(const TrackMeta& track, const LyricData& data) {
    if (data.sourceText.empty()) {
        return false;
    }

    std::string target = stripExtension(track.path) + ".lrc";
    return fs_.writeFile(target, data.sourceText);
}

}  // namespace openlyrics
