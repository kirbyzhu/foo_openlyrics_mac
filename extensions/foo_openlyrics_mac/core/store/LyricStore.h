#pragma once
#include "ports/FileSystem.h"
#include "model/TrackMeta.h"
#include "model/LyricData.h"

namespace openlyrics {

class LyricStore {
public:
    explicit LyricStore(FileSystem& fs);
    // 把 data.sourceText 无损写到 <track.path 去扩展名>.lrc。
    // sourceText 为空 → 不写，返回 false。
    // 目标 basename 在所在目录里已存在（大小写不敏感，APFS 默认大小写不敏感）→ 不覆盖，
    // 不写，返回 false——即便该已存在文件为空/损坏，也保留用户文件不动，代价是下次仍会
    // 走在线重新抓取（这一权衡是有意的，见 core/ports/FileSystem.h listDirectory 注释）。
    // 否则返回 writeFile 结果。
    bool save(const TrackMeta& track, const LyricData& data);

private:
    FileSystem& fs_;
};

}  // namespace openlyrics
