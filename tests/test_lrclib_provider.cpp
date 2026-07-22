#include <gtest/gtest.h>
#include "sources/LrcLibProvider.h"

using namespace openlyrics;

namespace {

class FakeHttp : public HttpClient {
public:
    std::string lastUrl;
    HttpResponse response;
    bool called = false;

    HttpResponse get(const std::string& url,
                      const std::vector<std::pair<std::string, std::string>>& = {}) override {
        called = true;
        lastUrl = url;
        return response;
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
