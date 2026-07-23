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
    bool fetch(const TrackMeta& track, LyricData& out) override;

private:
    HttpClient& http_;
    CryptoPort& crypto_;
};

}  // namespace openlyrics
