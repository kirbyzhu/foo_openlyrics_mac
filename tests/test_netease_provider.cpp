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
                      const std::vector<std::pair<std::string, std::string>>& = {},
                      CancelToken* = nullptr) override {
        getCalled = true;
        lastUrl = url;
        return {};
    }

    HttpResponse post(const std::string& url,
                       const std::string& body,
                       const std::vector<std::pair<std::string, std::string>>& = {},
                       CancelToken* = nullptr) override {
        ++postCount;
        lastUrl = url;
        lastBody = body;
        if (url.find("cloudsearch") != std::string::npos ||
            url.find("/search") != std::string::npos) return searchResp;
        if (url.find("/lyric/") != std::string::npos || url.find("/song/") != std::string::npos) return lyricResp;
        return {};
    }
};

// FakeCrypto: 记录调用参数并返回预置值。
class FakeCrypto : public CryptoPort {
public:
    struct EcbCall {
        std::string plain;
        std::string key;
    };
    std::vector<EcbCall> ecbCalls;
    std::string ecbResult;

    std::string md5Result;
    std::string md5LastInput;
    bool md5Called = false;

    std::string aes128EcbEncrypt(const std::string& plain,
                                  const std::string& key) override {
        ecbCalls.push_back({plain, key});
        return ecbResult;
    }

    std::string md5Hex(const std::string& input) override {
        md5Called = true;
        md5LastInput = input;
        return md5Result;
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

std::string makeYrcLyricResp(const std::string& yrcText, const std::string& lrcText) {
    std::string escapedYrc;
    for (char c : yrcText) {
        if (c == '"') escapedYrc += "\\\"";
        else escapedYrc += c;
    }
    return R"({"yrc":{"version":1,"lyric":")" + escapedYrc +
           R"("},"lrc":{"version":5,"lyric":")" + lrcText +
           R"("},"code":200})";
}

}  // namespace

// 端到端成功命中。
TEST(NetEaseProvider, FullHit) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";

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

TEST(NetEaseProvider, FetchByIdPrefersYrc) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";

    http.lyricResp.status = 200;
    http.lyricResp.body = makeYrcLyricResp(
        "{\"t\":1000,\"c\":[{\"tx\":\"Hello \",\"li\":1000,\"rc\":500},{\"tx\":\"world\",\"li\":1500,\"rc\":800}]}",
        "[00:01.00]hello world");

    NetEaseProvider provider(http, crypto);
    LyricData out;

    ASSERT_TRUE(provider.fetchById("12345", out));
    EXPECT_TRUE(out.hasSyllables());
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].syllables.size(), 2u);
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
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";

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
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";

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
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";

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
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";

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

// EAPI body 仅含 params=（不含 encSecKey）。
TEST(NetEaseProvider, EapiBodyIsJustParams) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";

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
    EXPECT_EQ(http.lastBody.find("encSecKey"), std::string::npos);
}

// EAPI 加密验证：MD5 输入包含 nobody/api/use/md5forencrypt。
TEST(NetEaseProvider, EapiEncryptionFlow) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";

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

    ASSERT_TRUE(crypto.md5Called);
    EXPECT_NE(crypto.md5LastInput.find("nobody"), std::string::npos);
    EXPECT_NE(crypto.md5LastInput.find("md5forencrypt"), std::string::npos);

    ASSERT_FALSE(crypto.ecbCalls.empty());
    EXPECT_EQ(crypto.ecbCalls[0].key, std::string("e82ckenh8dichen8", 16));
}

// EAPI 搜索 JSON 包含 cloudsearch 的 s/type 字段。
TEST(NetEaseProvider, EapiSearchParamsFormat) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.ecbResult = "01";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";

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

    ASSERT_FALSE(crypto.ecbCalls.empty());
    EXPECT_NE(crypto.ecbCalls[0].plain.find("\"s\""), std::string::npos);
    EXPECT_NE(crypto.ecbCalls[0].plain.find("\"type\""), std::string::npos);
}

// --- search() 测试 ---

TEST(NetEaseProvider, SearchReturnsMultipleCandidates) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";

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
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";

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
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";

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
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";

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

TEST(NetEaseProvider, ParsesUpToTenSongs) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";
    std::string songs;
    for (int i = 0; i < 10; ++i) {
        if (i) songs += ",";
        songs += "{\"id\":" + std::to_string(1000 + i) +
                 ",\"name\":\"n" + std::to_string(i) +
                 "\",\"ar\":[{\"name\":\"a\"}],\"al\":{\"name\":\"al\"},\"dt\":200000}";
    }
    http.searchResp.status = 200;
    http.searchResp.body = "{\"code\":200,\"result\":{\"songs\":[" + songs + "]}}";

    NetEaseProvider p(http, crypto);
    TrackMeta t; t.title = "n"; t.artist = "";
    std::vector<SearchResult> out;
    p.search(t, out);
    EXPECT_EQ(out.size(), 10u);
}

TEST(NetEaseProvider, QueryStripsParens) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.ecbResult = "AABBCCDD";
    crypto.md5Result = "deadbeef12345678deadbeef12345678";
    http.searchResp.status = 200;
    http.searchResp.body = "{\"code\":200,\"result\":{\"songs\":[]}}";

    NetEaseProvider p(http, crypto);
    TrackMeta t; t.title = "晴天 (Live)"; t.artist = "周杰伦";
    std::vector<SearchResult> out;
    p.search(t, out);
    // NetEase query 经 eapiEncrypt 加密，明文在 ecbEncrypt 的输入 plain 里：应含清理后的
    // "晴天"，不含被移除的 "Live"。
    ASSERT_FALSE(crypto.ecbCalls.empty());
    EXPECT_NE(crypto.ecbCalls[0].plain.find("晴天"), std::string::npos);
    EXPECT_EQ(crypto.ecbCalls[0].plain.find("Live"), std::string::npos);
}
