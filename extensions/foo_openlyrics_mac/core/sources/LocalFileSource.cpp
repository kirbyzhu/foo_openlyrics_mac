#include "sources/LocalFileSource.h"
#include "internal/PathUtils.h"

#include <algorithm>
#include <cctype>

namespace openlyrics {

using internal::dirOf;
using internal::basenameOf;
using internal::toLower;
using internal::ciEquals;
using internal::endsWithCi;

namespace {

// 模糊匹配阶段一个候选目录条目的排序信息，见 fetch() 里 Step 2 的排序规则 (a)-(d)。
struct FuzzyCandidate {
    std::string entry;
    bool hasArtist = false;
    bool isLrc = false;
    size_t normalizedLength = 0;
};

// Forward reference helper for normalize in comparator (defined below in class scope)
std::string NormalizeForComparator(const std::string& s);

bool RankFuzzyCandidate(const FuzzyCandidate& a, const FuzzyCandidate& b) {
    if (a.hasArtist != b.hasArtist) return a.hasArtist;                    // (a) 含 artist 优先
    if (a.isLrc != b.isLrc) return a.isLrc;                                // (b) .lrc 优先于 .txt
    if (a.normalizedLength != b.normalizedLength) return a.normalizedLength < b.normalizedLength;  // (c) 更短优先
    return NormalizeForComparator(a.entry) < NormalizeForComparator(b.entry);  // (d) 标准化后字典序最小者优先
}

}  // namespace

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

std::string LocalFileSource::normalize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c < 0x80) {
            // ASCII 范围：仅保留字母数字（小写），丢弃标点/空白
            if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
        } else {
            // 非 ASCII 字节（UTF-8 多字节的一部分）：保留原样
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

// Helper function for use within anonymous namespace
namespace {
std::string NormalizeForComparator(const std::string& s) {
    return LocalFileSource::normalize(s);
}
}  // namespace

bool LocalFileSource::resolvePath(const TrackMeta& track, std::string& outPath) {
    const std::string dir = dirOf(track.path);
    const std::string audioBase = basenameOf(LocalFileSource::stripExtension(track.path));
    const std::vector<std::string> entries = fs_.listDirectory(dir);
    if (entries.empty()) return false;

    static const char* kExts[] = {".lrc", ".txt"};

    // Step 1：精确/模式候选，大小写不敏感——真实文件系统区分大小写，逐条目做
    // 大小写归一比较，命中后用目录里的真实条目名去拼路径（而不是拼出来的候选名）。
    std::vector<std::string> candidates;
    if (!audioBase.empty()) candidates.push_back(audioBase);
    if (!track.title.empty()) candidates.push_back(track.title);
    if (!track.artist.empty() && !track.title.empty()) {
        candidates.push_back(track.artist + " - " + track.title);
    }

    for (const std::string& candidate : candidates) {
        for (const char* ext : kExts) {
            const std::string target = candidate + ext;
            for (const std::string& entry : entries) {
                if (!ciEquals(entry, target)) continue;
                outPath = dir + entry;
                return true;
            }
        }
    }

    // Step 2：模糊回退——扫描目录里所有 .lrc/.txt 条目，标准化后若包含标准化标题
    // 子串则纳入候选，再按 (a)-(d) 规则排序取最优。
    if (track.title.empty()) return false;
    const std::string normTitle = normalize(track.title);
    if (normTitle.empty()) return false;
    const std::string normArtist = normalize(track.artist);

    std::vector<FuzzyCandidate> matches;
    for (const std::string& entry : entries) {
        const std::string lowerEntry = toLower(entry);
        bool isLrc;
        if (endsWithCi(lowerEntry, ".lrc")) {
            isLrc = true;
        } else if (endsWithCi(lowerEntry, ".txt")) {
            isLrc = false;
        } else {
            continue;
        }

        const std::string normEntry = normalize(entry);
        if (normEntry.find(normTitle) == std::string::npos) continue;

        FuzzyCandidate fc;
        fc.entry = entry;
        fc.isLrc = isLrc;
        fc.hasArtist = !normArtist.empty() && normEntry.find(normArtist) != std::string::npos;
        fc.normalizedLength = normEntry.size();
        matches.push_back(std::move(fc));
    }

    if (matches.empty()) return false;

    std::sort(matches.begin(), matches.end(), RankFuzzyCandidate);
    outPath = dir + matches.front().entry;
    return true;
}

bool LocalFileSource::fetch(const TrackMeta& track, LyricData& out, CancelToken* cancel) {
    if (cancel && cancel->isCancelled()) return false;
    std::string path;
    if (!resolvePath(track, path)) return false;
    if (cancel && cancel->isCancelled()) return false;

    std::string text;
    if (fs_.readFile(path, text) && !text.empty()) {
        out = LrcParser::parse(text);
        return true;
    }
    return false;
}

}  // namespace openlyrics
