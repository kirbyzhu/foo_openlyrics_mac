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
    // 返回 false。
    bool save(const TrackMeta& track, const LyricData& data);

    // 与 save() 相同但跳过存在性检查，直接覆写已存在的 .lrc 文件。
    // 用于 offset 微调后写回、编辑器保存等会修改既有文件的场景。
    bool forceSave(const TrackMeta& track, const LyricData& data);

private:
    FileSystem& fs_;
};

}  // namespace openlyrics
