#pragma once
#include "sources/LyricSource.h"
#include "ports/HttpClient.h"

namespace openlyrics {

// 从 LrcLib（https://lrclib.net）按 artist/title/album/duration 检索歌词。
// 纯 C++，网络访问经由注入的 HttpClient 端口完成，便于用 FakeHttp 做 TDD。
class LrcLibProvider : public LyricSource {
public:
    explicit LrcLibProvider(HttpClient& http);
    bool fetch(const TrackMeta& track, LyricData& out) override;

private:
    HttpClient& http_;
};

}  // namespace openlyrics
