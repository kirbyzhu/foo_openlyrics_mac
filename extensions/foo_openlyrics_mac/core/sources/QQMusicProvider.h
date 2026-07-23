#pragma once
#include "sources/LyricSource.h"
#include "ports/HttpClient.h"
#include "ports/CryptoPort.h"

namespace openlyrics {

// 从 QQ 音乐按 artist+title 搜索并拉取歌词。
// 纯 C++，网络经由 HttpClient，解密经由 CryptoPort（3DES），便于 TDD。
class QQMusicProvider : public LyricSource {
public:
    QQMusicProvider(HttpClient& http, CryptoPort& crypto);

    bool search(const TrackMeta& track, std::vector<SearchResult>& out) override;
    bool fetchById(const std::string& id, LyricData& out) override;
    SourceId sourceId() const override { return SourceId::QQMusic; }

private:
    // 从搜索响应 JSON 中解析 data.song.list[] 数组
    bool extractSongList(const std::string& json, std::vector<SearchResult>& out, int limit);

    HttpClient& http_;
    CryptoPort& crypto_;
};

}  // namespace openlyrics
