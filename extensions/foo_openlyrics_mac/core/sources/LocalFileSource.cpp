#include "sources/LocalFileSource.h"

namespace openlyrics {

LocalFileSource::LocalFileSource(FileSystem& fs) : fs_(fs) {}

std::string LocalFileSource::stripExtension(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const size_t dot = path.find_last_of('.');
    // 只有当 '.' 落在最后一个路径分隔符之后（即属于文件名本身），才视为扩展名分隔符
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return path.substr(0, dot);
    }
    return path;
}

bool LocalFileSource::fetch(const TrackMeta& track, LyricData& out) {
    const std::string base = stripExtension(track.path);
    static const char* kCandidateExts[] = {".lrc", ".txt"};
    for (const char* ext : kCandidateExts) {
        const std::string candidate = base + ext;
        std::string text;
        if (fs_.exists(candidate) && fs_.readFile(candidate, text) && !text.empty()) {
            out = LrcParser::parse(text);
            return true;
        }
    }
    return false;
}

}  // namespace openlyrics
