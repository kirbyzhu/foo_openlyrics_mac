#pragma once
#include "sources/LyricSource.h"
#include "ports/TagIO.h"
#include "parser/LrcParser.h"

namespace openlyrics {

// 从内嵌标签读取歌词的源
class TagSource : public LyricSource {
public:
    explicit TagSource(TagIO& tagio);
    bool fetch(const TrackMeta& track, LyricData& out, CancelToken* cancel = nullptr) override;
    SourceId sourceId() const override { return SourceId::Tag; }

private:
    TagIO& tagio_;
};

}  // namespace openlyrics
