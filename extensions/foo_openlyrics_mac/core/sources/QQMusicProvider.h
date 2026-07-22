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
    // Base64 解码（内联实现，不依赖外部库）。
    static std::string base64Decode(const std::string& s);

    HttpClient& http_;
    CryptoPort& crypto_;
};

}  // namespace openlyrics
