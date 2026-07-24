#include <gtest/gtest.h>
#include "ports/CancelToken.h"
#include "ports/HttpClient.h"
#include "pipeline/SearchPipeline.h"
#include "pipeline/SearchCoordinator.h"
#include "sources/LyricSource.h"
#include "matching/Matcher.h"

using namespace openlyrics;

namespace {

class FakeCancelableHttp : public HttpClient {
public:
    HttpResponse response;
    bool called = false;

    HttpResponse get(const std::string&,
                      const std::vector<std::pair<std::string, std::string>>& = {},
                      CancelToken* cancel = nullptr) override {
        called = true;
        if (cancel && cancel->isCancelled()) return {};
        return response;
    }

    HttpResponse post(const std::string&, const std::string&,
                       const std::vector<std::pair<std::string, std::string>>& = {},
                       CancelToken* cancel = nullptr) override {
        called = true;
        if (cancel && cancel->isCancelled()) return {};
        return response;
    }
};

class SlowMockSource : public LyricSource {
public:
    bool called = false;
    SourceId sid = SourceId::Unknown;

    SlowMockSource(SourceId id) : sid(id) {}

    bool search(const TrackMeta&, std::vector<SearchResult>& out, CancelToken* cancel = nullptr) override {
        called = true;
        if (cancel && cancel->isCancelled()) return false;
        SearchResult sr;
        sr.id = "1";
        sr.trackName = "Test";
        sr.artistName = "Artist";
        sr.source = sid;
        out.push_back(sr);
        return true;
    }

    bool fetchById(const std::string&, LyricData& out, CancelToken* cancel = nullptr) override {
        if (cancel && cancel->isCancelled()) return false;
        out.lines.push_back({1000, "lyric text", {}});
        return true;
    }

    bool fetch(const TrackMeta& track, LyricData& out, CancelToken* cancel = nullptr) override {
        called = true;
        if (cancel && cancel->isCancelled()) return false;
        return LyricSource::fetch(track, out, cancel);
    }

    SourceId sourceId() const override { return sid; }
};

}  // namespace

TEST(CancelToken, BasicOperations) {
    CancelToken token;
    EXPECT_FALSE(token.isCancelled());
    token.cancel();
    EXPECT_TRUE(token.isCancelled());
}

TEST(CancelToken, HttpClientReturnsEmptyWhenCancelled) {
    FakeCancelableHttp http;
    http.response.status = 200;
    http.response.body = "ok";

    CancelToken token;
    token.cancel();

    HttpResponse r = http.get("https://example.com", {}, &token);
    EXPECT_TRUE(http.called);
    EXPECT_EQ(r.status, 0);
    EXPECT_TRUE(r.body.empty());
}

TEST(CancelToken, SearchPipelineShortCircuitsOnCancelled) {
    SlowMockSource source(SourceId::LrcLib);
    SearchPipeline pipeline({&source});

    CancelToken token;
    token.cancel();

    TrackMeta track;
    track.title = "Test";
    LyricData out;

    EXPECT_FALSE(pipeline.resolve(track, out, &token));
    EXPECT_FALSE(source.called);
}

TEST(CancelToken, SearchCoordinatorShortCircuitsOnCancelled) {
    SlowMockSource source(SourceId::NetEase);
    Matcher matcher;
    SearchCoordinator coordinator({&source}, matcher);

    CancelToken token;
    token.cancel();

    TrackMeta track;
    track.title = "Test";
    LyricData out;

    EXPECT_FALSE(coordinator.resolve(track, out, &token));
    EXPECT_FALSE(source.called);
}
