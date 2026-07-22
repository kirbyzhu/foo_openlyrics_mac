#pragma once
#include "sources/LyricSource.h"
#include "ports/HttpClient.h"
#include "ports/CryptoPort.h"

namespace openlyrics {

// 从网易云音乐按 artist+title 搜索并拉取歌词。
// 纯 C++，网络经由 HttpClient，加密经由 CryptoPort，便于 TDD。
class NetEaseProvider : public LyricSource {
public:
    NetEaseProvider(HttpClient& http, CryptoPort& crypto);
    bool fetch(const TrackMeta& track, LyricData& out) override;

private:
    // weapi 加密：输入 JSON 字符串，返回 {params, encSecKey}。
    struct WeapiResult {
        std::string params;
        std::string encSecKey;
    };
    WeapiResult weapiEncrypt(const std::string& json);

    // weapi POST 并返回响应 body；status!=200 返回空串。
    std::string weapiPost(const std::string& url, const std::string& json);

    HttpClient& http_;
    CryptoPort& crypto_;
};

}  // namespace openlyrics
