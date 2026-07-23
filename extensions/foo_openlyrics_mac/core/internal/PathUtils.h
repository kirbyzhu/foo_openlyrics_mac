#pragma once
#include <string>

namespace openlyrics {
namespace internal {

inline std::string dirOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);
}

inline std::string basenameOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

inline std::string toLower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

inline bool ciEquals(const std::string& a, const std::string& b) {
    return toLower(a) == toLower(b);
}

inline std::string stripExtension(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return path.substr(0, dot);
    }
    return path;
}

inline bool endsWithCi(const std::string& lowerS, const std::string& lowerSuffix) {
    if (lowerSuffix.size() > lowerS.size()) return false;
    return lowerS.compare(lowerS.size() - lowerSuffix.size(), lowerSuffix.size(), lowerSuffix) == 0;
}

}  // namespace internal
}  // namespace openlyrics
