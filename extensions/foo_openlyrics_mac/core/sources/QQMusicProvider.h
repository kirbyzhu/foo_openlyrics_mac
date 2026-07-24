#pragma once
#include "sources/LyricSource.h"
#include "ports/HttpClient.h"

namespace openlyrics {

// 从 QQ 音乐按 artist+title 搜索并拉取歌词。
// 纯 C++，网络经由 HttpClient；歌词为明文 base64，无需解密，便于 TDD。
class QQMusicProvider : public LyricSource {
public:
    explicit QQMusicProvider(HttpClient& http);

    bool search(const TrackMeta& track, std::vector<SearchResult>& out,
                CancelToken* cancel = nullptr) override;
    bool fetchById(const std::string& id, LyricData& out,
                   CancelToken* cancel = nullptr) override;
    SourceId sourceId() const override { return SourceId::QQMusic; }

private:
    // 从搜索响应 JSON 中解析 data.song.list[] 数组
    bool extractSongList(const std::string& json, std::vector<SearchResult>& out, int limit);

    HttpClient& http_;
};

}  // namespace openlyrics
