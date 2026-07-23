#include <gtest/gtest.h>
#include "sources/NetEaseProvider.h"

using namespace openlyrics;

namespace {

// FakeHttp: 用 URL 前缀匹配返回不同响应。
class FakeHttp : public HttpClient {
public:
    std::string lastUrl;
    std::string lastBody;
    HttpResponse searchResp;
    HttpResponse lyricResp;
    bool getCalled = false;
    int postCount = 0;

    HttpResponse get(const std::string& url,
                      const std::vector<std::pair<std::string, std::string>>& = {}) override {
        getCalled = true;
        lastUrl = url;
        return {};
    }

    HttpResponse post(const std::string& url,
                       const std::string& body,
                       const std::vector<std::pair<std::string, std::string>>& = {}) override {
        ++postCount;
        lastUrl = url;
        lastBody = body;
        if (url.find("/search/") != std::string::npos) return searchResp;
        return lyricResp;
    }
};

// FakeCrypto: 记录调用参数并返回预置值。
class FakeCrypto : public CryptoPort {
public:
    struct AesCall {
        std::string plain;
        std::string key;
        std::string iv;
    };

    std::vector<AesCall> aesCalls;
    std::string aesResult;

    struct RsaCall {
        std::string plain;
        std::string modulus;
        std::string exponent;
    };
    RsaCall lastRsaCall;
    std::string rsaResult;

    std::string aes128CbcEncrypt(const std::string& plain,
                                  const std::string& key,
                                  const std::string& iv) override {
        aesCalls.push_back({plain, key, iv});
        return aesResult;
    }

    std::string rsaRawEncrypt(const std::string& plain,
                                const std::string& modulusHex,
                                const std::string& exponentHex) override {
        lastRsaCall = {plain, modulusHex, exponentHex};
        return rsaResult;
    }

    std::string tripleDesEcbDecrypt(const std::string&, const std::string&) override {
        return {};
    }
    std::string md5Hex(const std::string&) override {
        return {};
    }
};

std::string makeSearchResp(int64_t songId) {
    return R"({"result":{"songs":[{"id":)" + std::to_string(songId) +
           R"(,"name":"test"}],"songCount":1},"code":200})";
}

std::string makeLyricResp(const std::string& lrcText) {
    return R"({"lrc":{"version":5,"lyric":")" + lrcText +
           R"("},"code":200})";
}

}  // namespace

// 端到端成功命中。
TEST(NetEaseProvider, FullHit) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, 'a');

    http.searchResp.status = 200;
    http.searchResp.body = makeSearchResp(12345);
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]hello\n[00:02.00]world");

    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "周杰伦";
    track.title = "晴天";
    LyricData out;

    ASSERT_TRUE(provider.fetch(track, out));
    EXPECT_EQ(out.lines.size(), 2u);
    EXPECT_FALSE(out.sourceText.empty());
}

// title 为空 → 返回 false。
TEST(NetEaseProvider, EmptyTitle) {
    FakeHttp http;
    FakeCrypto crypto;
    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.title = "";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
    EXPECT_EQ(http.postCount, 0);
}

// 搜索 code != 200 → 返回 false。
TEST(NetEaseProvider, SearchCodeNot200) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, 'a');

    http.searchResp.status = 200;
    http.searchResp.body = R"({"code":-1,"message":"error"})";

    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
}

// 搜索 HTTP 错误 → 返回 false。
TEST(NetEaseProvider, SearchHttpError) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, 'a');

    http.searchResp.status = 500;

    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
}

// 歌词 code != 200 → 返回 false。
TEST(NetEaseProvider, LyricCodeNot200) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, 'a');

    http.searchResp.status = 200;
    http.searchResp.body = makeSearchResp(1);
    http.lyricResp.status = 200;
    http.lyricResp.body = R"({"code":-1})";

    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
}

// 纯音乐标记 → 返回 false。
TEST(NetEaseProvider, NoLyric) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, 'a');

    http.searchResp.status = 200;
    http.searchResp.body = makeSearchResp(1);
    http.lyricResp.status = 200;
    http.lyricResp.body = R"({"code":200,"nolyric":true})";

    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
}

// weapi body 包含 params 和 encSecKey。
TEST(NetEaseProvider, WeapiParamsPresent) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\xAB\xCD";
    crypto.rsaResult = std::string(256, 'f');

    http.searchResp.status = 200;
    http.searchResp.body = makeSearchResp(999);
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]a");

    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "A";
    track.title = "B";
    LyricData out;
    provider.fetch(track, out);

    EXPECT_NE(http.lastBody.find("params="), std::string::npos);
    EXPECT_NE(http.lastBody.find("encSecKey="), std::string::npos);
}

// 双层 AES 加密：第一层用 presetKey。
TEST(NetEaseProvider, DoubleAesEncryption) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, '0');

    http.searchResp.status = 200;
    http.searchResp.body = makeSearchResp(1);
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]a");

    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;
    provider.fetch(track, out);

    EXPECT_GE(crypto.aesCalls.size(), 2u);
    EXPECT_EQ(crypto.aesCalls[0].key, std::string("0CoJUm6Qyw8W8jud", 16));
    EXPECT_EQ(crypto.aesCalls[0].iv, std::string("0102030405060708", 16));
}

// RSA 加密参数校验。
TEST(NetEaseProvider, RsaParams) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, '0');

    http.searchResp.status = 200;
    http.searchResp.body = makeSearchResp(1);
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]a");

    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;
    provider.fetch(track, out);

    ASSERT_FALSE(crypto.lastRsaCall.plain.empty());
    EXPECT_GE(crypto.lastRsaCall.modulus.size(), 256u);
    EXPECT_EQ(crypto.lastRsaCall.exponent, "010001");
}

// --- search() 测试 ---

TEST(NetEaseProvider, SearchReturnsMultipleCandidates) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, 'a');

    // 构造含 2 首歌曲的搜索响应
    http.searchResp.status = 200;
    http.searchResp.body = R"({"result":{"songs":[
        {"id":111,"name":"Song A","ar":[{"name":"Artist A"}],"al":{"name":"Album A"},"dt":200000},
        {"id":222,"name":"Song B","ar":[{"name":"Artist B"}],"al":{"name":"Album B"},"dt":250000}
    ],"songCount":2},"code":200})";

    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "Artist A";
    track.title = "Song A";

    std::vector<SearchResult> results;
    ASSERT_TRUE(provider.search(track, results));
    ASSERT_GE(results.size(), 2u);
    EXPECT_EQ(results[0].id, "111");
    EXPECT_EQ(results[0].trackName, "Song A");
    EXPECT_EQ(results[0].artistName, "Artist A");
    EXPECT_EQ(results[0].albumName, "Album A");
    EXPECT_EQ(results[0].durationSec, 200);
    EXPECT_EQ(results[0].source, SourceId::NetEase);
}

TEST(NetEaseProvider, SearchEmptyTitle) {
    FakeHttp http;
    FakeCrypto crypto;
    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.title = "";
    std::vector<SearchResult> results;
    EXPECT_FALSE(provider.search(track, results));
}

// --- fetchById() 测试 ---

TEST(NetEaseProvider, FetchByIdValid) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, 'a');

    // fetchById 只发歌词请求（1 次 POST）
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]hello\n[00:02.00]world");

    NetEaseProvider provider(http, crypto);
    LyricData out;
    ASSERT_TRUE(provider.fetchById("12345", out));
    EXPECT_EQ(out.lines.size(), 2u);
}

TEST(NetEaseProvider, FetchByIdEmptyId) {
    FakeHttp http;
    FakeCrypto crypto;
    NetEaseProvider provider(http, crypto);
    LyricData out;
    EXPECT_FALSE(provider.fetchById("", out));
}

TEST(NetEaseProvider, FetchByIdNoLyric) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, 'a');

    http.lyricResp.status = 200;
    http.lyricResp.body = R"({"code":200,"nolyric":true})";

    NetEaseProvider provider(http, crypto);
    LyricData out;
    EXPECT_FALSE(provider.fetchById("12345", out));
}

// --- fetch() 使用基类默认实现：search → fetchById ---

TEST(NetEaseProvider, FetchUsesDefaultImpl) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, 'a');

    // search 成功 + fetchById 成功
    http.searchResp.status = 200;
    http.searchResp.body = R"({"result":{"songs":[
        {"id":999,"name":"Test","ar":[{"name":"Tester"}],"al":{"name":"Test Album"},"dt":180000}
    ],"songCount":1},"code":200})";
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]a\n[00:02.00]b");

    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "Tester";
    track.title = "Test";
    LyricData out;

    ASSERT_TRUE(provider.fetch(track, out));
    EXPECT_EQ(out.lines.size(), 2u);
}

// sourceId() 返回 NetEase
TEST(NetEaseProvider, SourceIdIsNetEase) {
    FakeHttp http;
    FakeCrypto crypto;
    NetEaseProvider provider(http, crypto);
    EXPECT_EQ(provider.sourceId(), SourceId::NetEase);
}
