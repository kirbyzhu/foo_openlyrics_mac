#include <gtest/gtest.h>
#include "sources/LrcLibProvider.h"
#include "model/SearchResult.h"

using namespace openlyrics;

namespace {

class FakeHttp : public HttpClient {
public:
    std::string lastUrl;
    HttpResponse response;
    bool called = false;

    HttpResponse get(const std::string& url,
                      const std::vector<std::pair<std::string, std::string>>& = {},
                      CancelToken* = nullptr) override {
        called = true;
        lastUrl = url;
        return response;
    }
    HttpResponse post(const std::string&, const std::string&,
                       const std::vector<std::pair<std::string, std::string>>& = {},
                       CancelToken* = nullptr) override {
        return {};
    }
};

}  // namespace

TEST(LrcLibProvider, SyncedHit) {
    FakeHttp http;
    http.response.status = 200;
    http.response.body =
        R"({"instrumental":false,"syncedLyrics":"[00:01.00]a\n[00:03.00]b","plainLyrics":""})";
    LrcLibProvider provider(http);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    ASSERT_TRUE(provider.fetch(track, out));
    EXPECT_TRUE(out.synced);
    ASSERT_EQ(out.lines.size(), 2u);
    EXPECT_EQ(out.sourceText, "[00:01.00]a\n[00:03.00]b");
}

TEST(LrcLibProvider, PlainOnlyHit) {
    FakeHttp http;
    http.response.status = 200;
    http.response.body = R"({"instrumental":false,"syncedLyrics":"","plainLyrics":"la la"})";
    LrcLibProvider provider(http);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    ASSERT_TRUE(provider.fetch(track, out));
    EXPECT_FALSE(out.synced);
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "la la");
}

TEST(LrcLibProvider, Instrumental) {
    FakeHttp http;
    http.response.status = 200;
    http.response.body =
        R"({"instrumental":true,"syncedLyrics":"","plainLyrics":""})";
    LrcLibProvider provider(http);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
}

TEST(LrcLibProvider, NotFound404) {
    FakeHttp http;
    http.response.status = 404;
    http.response.body = R"({"code":404,"message":"not found"})";
    LrcLibProvider provider(http);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
}

TEST(LrcLibProvider, NetworkFailure) {
    FakeHttp http;
    http.response.status = 0;
    http.response.body = "";
    LrcLibProvider provider(http);
    TrackMeta track;
    track.artist = "x";
    track.title = "y";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
}

TEST(LrcLibProvider, EmptyTitleSkipsHttp) {
    FakeHttp http;
    LrcLibProvider provider(http);
    TrackMeta track;
    track.artist = "x";
    track.title = "";
    LyricData out;

    EXPECT_FALSE(provider.fetch(track, out));
    EXPECT_FALSE(http.called);
    EXPECT_TRUE(http.lastUrl.empty());
}

TEST(LrcLibProvider, UrlEncodingAndParams) {
    FakeHttp http;
    http.response.status = 200;
    http.response.body = R"({"instrumental":false,"syncedLyrics":"","plainLyrics":""})";
    LrcLibProvider provider(http);
    TrackMeta track;
    track.artist = "伍佰";
    track.title = "突然的自我";
    track.album = "";
    track.lengthMs = 213000;
    LyricData out;

    provider.fetch(track, out);
    EXPECT_EQ(http.lastUrl,
              "https://lrclib.net/api/get?artist_name=%E4%BC%8D%E4%BD%B0"
              "&track_name=%E7%AA%81%E7%84%B6%E7%9A%84%E8%87%AA%E6%88%91"
              "&duration=213");
}

TEST(LrcLibProvider, OmitsDurationWhenLengthUnknown) {
    FakeHttp http;
    http.response.status = 200;
    http.response.body = R"({"instrumental":false,"syncedLyrics":"","plainLyrics":""})";
    LrcLibProvider provider(http);
    TrackMeta track;
    track.artist = "A";
    track.title = "B";
    track.album = "";
    track.lengthMs = 0;
    LyricData out;

    provider.fetch(track, out);
    EXPECT_EQ(http.lastUrl.find("duration"), std::string::npos);
    EXPECT_EQ(http.lastUrl.find("album_name"), std::string::npos);
}

// --- search() tests ---

TEST(LrcLibProvider, SearchHit) {
    FakeHttp http;
    http.response.status = 200;
    http.response.body =
        R"([{"id":123,"trackName":"测试歌曲","artistName":"歌手",)"
        R"("albumName":"专辑","duration":240}])";
    LrcLibProvider provider(http);
    std::vector<SearchResult> results;
    ASSERT_TRUE(provider.search("测试", results));
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].id, "123");
    EXPECT_EQ(results[0].trackName, "测试歌曲");
    EXPECT_EQ(results[0].artistName, "歌手");
    EXPECT_EQ(results[0].albumName, "专辑");
    EXPECT_EQ(results[0].durationSec, 240);
}

TEST(LrcLibProvider, SearchEmptyQuery) {
    FakeHttp http;
    LrcLibProvider provider(http);
    std::vector<SearchResult> results;
    EXPECT_FALSE(provider.search("", results));
    EXPECT_FALSE(http.called);
}

TEST(LrcLibProvider, SearchHttpError) {
    FakeHttp http;
    http.response.status = 500;
    LrcLibProvider provider(http);
    std::vector<SearchResult> results;
    EXPECT_FALSE(provider.search("abc", results));
}

TEST(LrcLibProvider, SearchEmptyArray) {
    FakeHttp http;
    http.response.status = 200;
    http.response.body = "[]";
    LrcLibProvider provider(http);
    std::vector<SearchResult> results;
    EXPECT_FALSE(provider.search("nothing", results));
    EXPECT_TRUE(results.empty());
}

TEST(LrcLibProvider, SearchMultipleResults) {
    FakeHttp http;
    http.response.status = 200;
    http.response.body =
        R"([{"id":1,"trackName":"A","artistName":"X","albumName":"P","duration":100},)"
        R"({"id":2,"trackName":"B","artistName":"Y","albumName":"Q","duration":200}])";
    LrcLibProvider provider(http);
    std::vector<SearchResult> results;
    ASSERT_TRUE(provider.search("ab", results));
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].id, "1");
    EXPECT_EQ(results[1].id, "2");
}

// --- fetchById() tests ---

TEST(LrcLibProvider, FetchByIdHit) {
    FakeHttp http;
    http.response.status = 200;
    http.response.body =
        R"({"instrumental":false,"syncedLyrics":"[00:05.00]hello","plainLyrics":""})";
    LrcLibProvider provider(http);
    LyricData out;
    ASSERT_TRUE(provider.fetchById(42, out));
    EXPECT_TRUE(out.synced);
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "hello");
    EXPECT_NE(http.lastUrl.find("id=42"), std::string::npos);
}

TEST(LrcLibProvider, FetchByIdInvalidId) {
    FakeHttp http;
    LrcLibProvider provider(http);
    LyricData out;
    EXPECT_FALSE(provider.fetchById(0, out));
    EXPECT_FALSE(provider.fetchById(-1, out));
    EXPECT_FALSE(http.called);
}

TEST(LrcLibProvider, FetchByIdNotFound) {
    FakeHttp http;
    http.response.status = 404;
    LrcLibProvider provider(http);
    LyricData out;
    EXPECT_FALSE(provider.fetchById(99999, out));
}
