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
    SourceId sourceId() const override { return SourceId::Local; }

    // 只做匹配、不读内容：命中则把命中歌词文件的完整路径写入 outPath 并返回 true。
    // 与 fetch 共用同一套精确+模糊匹配逻辑，供"删除当前歌词文件"定位真实来源文件用。
    bool resolvePath(const TrackMeta& track, std::string& outPath);

    // 去掉路径最后一段（文件名部分）的扩展名：取最后一个 '/' 之后的子串中
    // 最后一个 '.'；若该子串无 '.'，视为无扩展名，原样返回整条路径。
    static std::string stripExtension(const std::string& path);

    // 标准化：转小写 + 丢弃所有非字母数字字符（空格、标点、连字符……），
    // 供模糊匹配时比较"标题子串是否包含在文件名里"用。
    static std::string normalize(const std::string& s);

private:
    FileSystem& fs_;
};

}  // namespace openlyrics
