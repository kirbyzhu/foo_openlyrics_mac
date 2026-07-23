#pragma once
#include "sources/LyricSource.h"
#include "ports/HttpClient.h"
#include "ports/CryptoPort.h"

namespace openlyrics {

// 从网易云音乐按 artist+title 搜索并拉取歌词。
// EAPI 协议：AES-128-ECB + MD5 签名加密，endpoint interface.music.163.com。
// 纯 C++，网络经由 HttpClient，加密经由 CryptoPort，便于 TDD。
class NetEaseProvider : public LyricSource {
public:
    NetEaseProvider(HttpClient& http, CryptoPort& crypto);

    bool search(const TrackMeta& track, std::vector<SearchResult>& out) override;
    bool fetchById(const std::string& id, LyricData& out) override;
    SourceId sourceId() const override { return SourceId::NetEase; }
    // fetch() 使用基类默认实现：search → 取第一候选 → fetchById

private:
    // EAPI 加密：path 含 /eapi/ 前缀，json 为紧凑 JSON params。
    // 返回大写 hex 编码的密文，空串表示加密失败。
    std::string eapiEncrypt(const std::string& path, const std::string& json);

    // EAPI POST：加密 → URL encode → POST → status 检查。
    // urlPath 为路径部分（如 "/eapi/search/song/list/page"），
    // 函数自动拼接完整 URL 并设置所需 headers。
    std::string eapiPost(const std::string& urlPath, const std::string& json);

    // 从搜索响应 JSON 中提取 songs 数组的多条候选。
    bool extractSongs(const std::string& json, std::vector<SearchResult>& out, int limit);

    // 从单首歌曲 JSON 对象中提取 id/name/ar/al/dt 字段。
    SearchResult parseSongObject(const std::string& objJson);

    HttpClient& http_;
    CryptoPort& crypto_;
};

}  // namespace openlyrics
