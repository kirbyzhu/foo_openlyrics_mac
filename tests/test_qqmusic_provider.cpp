#include <gtest/gtest.h>
#include "sources/QQMusicProvider.h"

using namespace openlyrics;

namespace {

class FakeHttp : public HttpClient {
public:
    std::string lastUrl;
    HttpResponse searchResp;
    HttpResponse lyricResp;
    int getCount = 0;

    HttpResponse get(const std::string& url,
                      const std::vector<std::pair<std::string, std::string>>& = {},
                      CancelToken* = nullptr) override {
        ++getCount;
        lastUrl = url;
        if (url.find("/soso/") != std::string::npos) return searchResp;
        return lyricResp;
    }
    HttpResponse post(const std::string&, const std::string&,
                       const std::vector<std::pair<std::string, std::string>>& = {},
                       CancelToken* = nullptr) override {
        return {};
    }
};

class FakeCrypto : public CryptoPort {
public:
    std::string aes128CbcEncrypt(const std::string&, const std::string&, const std::string&) override { return {}; }
    std::string aes128EcbEncrypt(const std::string&, const std::string&) override { return {}; }
    std::string rsaRawEncrypt(const std::string&, const std::string&, const std::string&) override { return {}; }
    std::string tripleDesEcbDecrypt(const std::string&, const std::string&) override { return {}; }
    std::string md5Hex(const std::string&) override { return {}; }
};

// 构造搜索响应，含指定 songmid。
std::string makeSearchResp(const std::string& mid) {
    return R"({"code":0,"data":{"song":{"list":[{"songmid":")" + mid +
           R"(","songname":"test"}],"totalnum":1}}})";
}

// 构造歌词响应，lyric 字段为 LRC 文本的 base64。
std::string makeLyricResp(const std::string& lrcText) {
    // 简单 base64 编码（使用标准字母表）。
    static const char* kTbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string enc;
    enc.reserve(((lrcText.size() + 2) / 3) * 4);
    for (size_t i = 0; i < lrcText.size(); i += 3) {
        unsigned char a = static_cast<unsigned char>(lrcText[i]);
        unsigned char b = (i + 1 < lrcText.size()) ? static_cast<unsigned char>(lrcText[i + 1]) : 0;
        unsigned char c = (i + 2 < lrcText.size()) ? static_cast<unsigned char>(lrcText[i + 2]) : 0;
        enc.push_back(kTbl[a >> 2]);
        enc.push_back(kTbl[((a & 0x03) << 4) | (b >> 4)]);
        enc.push_back((i + 1 < lrcText.size()) ? kTbl[((b & 0x0F) << 2) | (c >> 6)] : '=');
        enc.push_back((i + 2 < lrcText.size()) ? kTbl[c & 0x3F] : '=');
    }
    return R"({"code":0,"lyric":")" + enc + R"("})";
}

}  // namespace

// 端到端成功。
TEST(QQMusicProvider, FullHit) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 200;
    http.searchResp.body = makeSearchResp("001RaE0n4RrGX9");
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]hello\n[00:02.00]world");

    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "周杰伦";
    track.title = "晴天";
    LyricData out;

    ASSERT_TRUE(provider.fetch(track, out));
    EXPECT_EQ(out.lines.size(), 2u);
    EXPECT_FALSE(out.sourceText.empty());
}

// title 为空 → false。
TEST(QQMusicProvider, EmptyTitle) {
    FakeHttp http;
    FakeCrypto crypto;
    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.title = "";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
    EXPECT_EQ(http.getCount, 0);
}

// 搜索失败 → false。
TEST(QQMusicProvider, SearchHttpError) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 500;

    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
}

// 搜索无结果 → false。
TEST(QQMusicProvider, SearchNoResult) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 200;
    http.searchResp.body = R"({"code":0,"data":{"song":{"list":[],"totalnum":0}}})";

    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
}

// 歌词 code != 0 → false。
TEST(QQMusicProvider, LyricCodeNotZero) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 200;
    http.searchResp.body = makeSearchResp("mid1");
    http.lyricResp.status = 200;
    http.lyricResp.body = R"({"code":-1})";

    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
}

// 歌词 lyric 字段为空 → false。
TEST(QQMusicProvider, LyricEmpty) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 200;
    http.searchResp.body = makeSearchResp("mid1");
    http.lyricResp.status = 200;
    http.lyricResp.body = R"({"code":0,"lyric":""})";

    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
}

// 歌词 HTTP 失败 → false。
TEST(QQMusicProvider, LyricHttpError) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 200;
    http.searchResp.body = makeSearchResp("mid1");
    http.lyricResp.status = 500;

    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
}

// 搜索 URL 包含编码后的查询词。
TEST(QQMusicProvider, SearchUrlContainsQuery) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 200;
    http.searchResp.body = makeSearchResp("mid1");
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]a");

    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "阿";
    track.title = "波";
    LyricData out;
    provider.fetch(track, out);

    EXPECT_NE(http.lastUrl.find("c.y.qq.com"), std::string::npos);
    EXPECT_GE(http.getCount, 2);  // 搜索（含 title-only 回退）+ 取词
}

// --- search() 测试 ---

TEST(QQMusicProvider, SearchReturnsMultipleCandidates) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 200;
    http.searchResp.body = R"({"code":0,"data":{"song":{"list":[
        {"songmid":"mid1","songname":"Song A","singer":[{"name":"Artist A"}],"albumname":"Album A","interval":200},
        {"songmid":"mid2","songname":"Song B","singer":[{"name":"Artist B"}],"albumname":"Album B","interval":250}
    ],"totalnum":2}}})";

    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "Artist A";
    track.title = "Song A";

    std::vector<SearchResult> results;
    ASSERT_TRUE(provider.search(track, results));
    ASSERT_GE(results.size(), 2u);
    EXPECT_EQ(results[0].id, "mid1");
    EXPECT_EQ(results[0].trackName, "Song A");
    EXPECT_EQ(results[0].artistName, "Artist A");
    EXPECT_EQ(results[0].albumName, "Album A");
    EXPECT_EQ(results[0].durationSec, 200);
    EXPECT_EQ(results[0].source, SourceId::QQMusic);
}

TEST(QQMusicProvider, SearchEmptyTitle) {
    FakeHttp http;
    FakeCrypto crypto;
    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.title = "";
    std::vector<SearchResult> results;
    EXPECT_FALSE(provider.search(track, results));
}

// --- fetchById() 测试 ---

TEST(QQMusicProvider, FetchByIdValid) {
    FakeHttp http;
    FakeCrypto crypto;
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]hello\n[00:02.00]world");

    QQMusicProvider provider(http, crypto);
    LyricData out;
    ASSERT_TRUE(provider.fetchById("mid1", out));
    EXPECT_EQ(out.lines.size(), 2u);
}

TEST(QQMusicProvider, FetchByIdEmptyId) {
    FakeHttp http;
    FakeCrypto crypto;
    QQMusicProvider provider(http, crypto);
    LyricData out;
    EXPECT_FALSE(provider.fetchById("", out));
}

// --- fetch() 使用基类默认实现 ---

TEST(QQMusicProvider, FetchUsesDefaultImpl) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 200;
    http.searchResp.body = R"({"code":0,"data":{"song":{"list":[
        {"songmid":"mid999","songname":"Test","singer":[{"name":"Tester"}],"albumname":"Test Album","interval":180}
    ],"totalnum":1}}})";
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]a\n[00:02.00]b");

    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "Tester";
    track.title = "Test";
    LyricData out;

    ASSERT_TRUE(provider.fetch(track, out));
    EXPECT_EQ(out.lines.size(), 2u);
}

// sourceId() 返回 QQMusic
TEST(QQMusicProvider, SourceIdIsQQMusic) {
    FakeHttp http;
    FakeCrypto crypto;
    QQMusicProvider provider(http, crypto);
    EXPECT_EQ(provider.sourceId(), SourceId::QQMusic);
}

TEST(QQMusicProvider, ParsesUpToTenSongs) {
    FakeHttp http;
    FakeCrypto crypto;
    std::string list;
    for (int i = 0; i < 10; ++i) {
        if (i) list += ",";
        list += "{\"songmid\":\"m" + std::to_string(i) +
                "\",\"songname\":\"n\",\"singer\":[{\"name\":\"a\"}],"
                "\"albumname\":\"al\",\"interval\":200}";
    }
    http.searchResp.status = 200;
    http.searchResp.body = "{\"code\":0,\"data\":{\"song\":{\"list\":[" + list + "]}}}";

    QQMusicProvider p(http, crypto);
    TrackMeta t; t.title = "n"; t.artist = "";
    std::vector<SearchResult> out;
    p.search(t, out);
    EXPECT_EQ(out.size(), 10u);
}

TEST(QQMusicProvider, QueryStripsParens) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 200;
    http.searchResp.body = "{\"code\":0,\"data\":{\"song\":{\"list\":[]}}}";

    QQMusicProvider p(http, crypto);
    TrackMeta t; t.title = "晴天 (Live)"; t.artist = "周杰伦";
    std::vector<SearchResult> out;
    p.search(t, out);
    // QQ query 在 URL 的 w= 参数中，urlEncode 后中文/空格被转义；
    // 断言未编码的 "Live" 不出现（括号内容已被移除）
    EXPECT_EQ(http.lastUrl.find("Live"), std::string::npos);
}
