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

    bool search(const TrackMeta& track, std::vector<SearchResult>& out) override;
    bool fetchById(const std::string& id, LyricData& out) override;
    SourceId sourceId() const override { return SourceId::NetEase; }
    // fetch() 使用基类默认实现：search → 取第一候选 → fetchById

    // 诊断：最近一次 search()/fetchById() 的失败原因。空串表示无错误。
    std::string lastDiag;

private:
    // weapi 加密：输入 JSON 字符串，返回 {params, encSecKey}。
    struct WeapiResult {
        std::string params;
        std::string encSecKey;
    };
    WeapiResult weapiEncrypt(const std::string& json);

    // weapi POST 并返回响应 body；status!=200 返回空串。
    std::string weapiPost(const std::string& url, const std::string& json);

    // 从搜索响应 JSON 中提取 songs 数组的多条候选。
    // 复用 extractSongs 解析 result.songs[] 的前 limit 条。
    bool extractSongs(const std::string& json, std::vector<SearchResult>& out, int limit);

    // 从单首歌曲 JSON 对象中提取 id/name/ar/al/dt 字段。
    SearchResult parseSongObject(const std::string& objJson);

    HttpClient& http_;
    CryptoPort& crypto_;
};

}  // namespace openlyrics
