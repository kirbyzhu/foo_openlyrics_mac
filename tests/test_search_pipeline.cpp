#include <gtest/gtest.h>
#include "pipeline/SearchPipeline.h"
#include "sources/LyricSource.h"

using namespace openlyrics;

namespace {

// 记录是否被调用的桩源，用于验证短路逻辑
class StubSource : public LyricSource {
public:
    bool shouldHit = false;
    bool called = false;
    LyricData toReturn;
    bool fetch(const TrackMeta&, LyricData& out) override {
        called = true;
        if (!shouldHit) return false;
        out = toReturn;
        return true;
    }
};

}  // namespace

TEST(SearchPipeline, FirstSourceHitsShortCircuits) {
    StubSource first;
    first.shouldHit = true;
    first.toReturn.lines.push_back({1000, "first hit", {}});
    StubSource second;
    second.shouldHit = true;

    SearchPipeline pipeline({&first, &second});
    TrackMeta track;
    LyricData out;
    ASSERT_TRUE(pipeline.resolve(track, out));
    EXPECT_TRUE(first.called);
    EXPECT_FALSE(second.called);
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "first hit");
}

TEST(SearchPipeline, FirstMissesSecondHits) {
    StubSource first;
    first.shouldHit = false;
    StubSource second;
    second.shouldHit = true;
    second.toReturn.lines.push_back({2000, "second hit", {}});

    SearchPipeline pipeline({&first, &second});
    TrackMeta track;
    LyricData out;
    ASSERT_TRUE(pipeline.resolve(track, out));
    EXPECT_TRUE(first.called);
    EXPECT_TRUE(second.called);
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "second hit");
}

TEST(SearchPipeline, AllMissReturnsFalse) {
    StubSource first;
    first.shouldHit = false;
    StubSource second;
    second.shouldHit = false;

    SearchPipeline pipeline({&first, &second});
    TrackMeta track;
    LyricData out;
    EXPECT_FALSE(pipeline.resolve(track, out));
    EXPECT_TRUE(first.called);
    EXPECT_TRUE(second.called);
}
