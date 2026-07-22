#pragma once
#include "sources/LyricSource.h"
#include "ports/FileSystem.h"
#include "parser/LrcParser.h"
#include <string>

namespace openlyrics {

// 在音轨文件同目录查找 .lrc（优先）/.txt 歌词文件的源
class LocalFileSource : public LyricSource {
public:
    explicit LocalFileSource(FileSystem& fs);
    bool fetch(const TrackMeta& track, LyricData& out) override;

    // 去掉路径最后一段（文件名部分）的扩展名：取最后一个 '/' 之后的子串中
    // 最后一个 '.'；若该子串无 '.'，视为无扩展名，原样返回整条路径。
    static std::string stripExtension(const std::string& path);

private:
    FileSystem& fs_;
};

}  // namespace openlyrics
