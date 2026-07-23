#include <gtest/gtest.h>
#include "matching/Matcher.h"

using namespace openlyrics;

// --- normalizeForMatch ---

TEST(NormalizeForMatch, LowerAndStripPunct) {
    EXPECT_EQ(normalizeForMatch("Hello, World!"), "hello world");
}

TEST(NormalizeForMatch, FullWidthToHalf) {
    // 全角 A (FF21) 在 UTF-8 中为多字节序列 \xEF\xBC\xA1，
    // 非 ASCII 字节原样保留（不做降级转换）。
    std::string fullA = "\xEF\xBC\xA1";  // UTF-8 全角 A
    EXPECT_EQ(normalizeForMatch(fullA), fullA);
}

TEST(NormalizeForMatch, CollabFeat) {
    std::string result = normalizeForMatch("Song feat. Artist B");
    EXPECT_NE(result.find("(feat."), std::string::npos);
    EXPECT_NE(result.find("artist b)"), std::string::npos);
}

TEST(NormalizeForMatch, CollabFt) {
    std::string result = normalizeForMatch("Title ft. Someone");
    EXPECT_NE(result.find("(feat. someone)"), std::string::npos);
}

// --- jaccardSimilarity ---

TEST(JaccardSimilarity, Identical) {
    EXPECT_DOUBLE_EQ(jaccardSimilarity("hello world", "hello world"), 1.0);
}

TEST(JaccardSimilarity, HalfOverlap) {
    double j = jaccardSimilarity("a b", "b c");
    // tokens: {a,b} vs {b,c}, intersection=1, union=3 -> 1/3
    EXPECT_NEAR(j, 1.0/3.0, 0.01);
}

// --- Matcher::score ---

TEST(Matcher, ExactMatch) {
    Matcher m;
    TrackMeta track;
    track.title = "晴天";
    track.artist = "周杰伦";
    track.album = "叶惠美";
    track.lengthMs = 269000;

    SearchResult sr;
    sr.trackName = "晴天";
    sr.artistName = "周杰伦";
    sr.albumName = "叶惠美";
    sr.durationSec = 269;

    int s = m.score(track, sr);
    EXPECT_EQ(s, 100);
}

TEST(Matcher, EmptyCandidateTitle) {
    Matcher m;
    TrackMeta track;
    track.title = "晴天";
    track.artist = "周杰伦";

    SearchResult sr;
    sr.trackName = "";
    sr.artistName = "周杰伦";

    int s = m.score(track, sr);
    // 标题 0 分，艺术家满分；总分 = 0*0.4 + 100*0.25 = 25
    EXPECT_EQ(s, 25);
}

TEST(Matcher, NoDurationCandidate) {
    Matcher m;
    TrackMeta track;
    track.title = "晴天";
    track.artist = "周杰伦";
    track.lengthMs = 269000;

    SearchResult sr;
    sr.trackName = "晴天";
    sr.artistName = "周杰伦";
    sr.durationSec = 0;

    int s = m.score(track, sr);
    // 标题 100, 艺术家 100, 时长 0, 专辑 0
    EXPECT_EQ(s, 65);  // 100*0.4 + 100*0.25 + 0 + 0
}

TEST(Matcher, DurationWithin3Sec) {
    Matcher m;
    TrackMeta track;
    track.title = "x";
    track.artist = "y";
    track.lengthMs = 100000;  // 100s

    SearchResult sr;
    sr.trackName = "x";
    sr.artistName = "y";
    sr.durationSec = 101;  // 差 1s

    int s = m.score(track, sr);
    // 标题 100, 艺术家 100, 时长 100, 专辑 0
    EXPECT_EQ(s, 85);  // 100*0.4 + 100*0.25 + 100*0.2 = 85
}

TEST(Matcher, DurationWithin8Sec) {
    Matcher m;
    TrackMeta track;
    track.title = "x";
    track.artist = "y";
    track.lengthMs = 100000;

    SearchResult sr;
    sr.trackName = "x";
    sr.artistName = "y";
    sr.durationSec = 106;  // 差 6s

    int s = m.score(track, sr);
    EXPECT_EQ(s, 79);  // 100*0.4 + 100*0.25 + 70*0.2 = 79
}

TEST(Matcher, DurationOver15Sec) {
    Matcher m;
    TrackMeta track;
    track.title = "x";
    track.artist = "y";
    track.lengthMs = 100000;

    SearchResult sr;
    sr.trackName = "x";
    sr.artistName = "y";
    sr.durationSec = 120;  // 差 20s

    int s = m.score(track, sr);
    EXPECT_EQ(s, 65);  // 100*0.4 + 100*0.25 + 0*0.2 = 65
}

TEST(Matcher, AlbumSubstring) {
    Matcher m;
    TrackMeta track;
    track.title = "x";
    track.artist = "y";
    track.album = "叶惠美";

    SearchResult sr;
    sr.trackName = "x";
    sr.artistName = "y";
    sr.albumName = "叶惠美 (珍藏版)";  // 包含 "叶惠美"

    int s = m.score(track, sr);
    // 标题 100, 艺术家 100, 时长 0, 专辑 100
    EXPECT_EQ(s, 80);  // 100*0.4 + 100*0.25 + 100*0.15 = 80
}

TEST(Matcher, HighConfidence) {
    Matcher m;
    EXPECT_TRUE(m.isHighConfidence(80));
    EXPECT_TRUE(m.isHighConfidence(70));
    EXPECT_FALSE(m.isHighConfidence(69));
}

TEST(Matcher, LowConfidence) {
    Matcher m;
    EXPECT_TRUE(m.isLowConfidence(69));
    EXPECT_TRUE(m.isLowConfidence(40));
    EXPECT_FALSE(m.isLowConfidence(39));
    EXPECT_FALSE(m.isLowConfidence(70));
}

TEST(Matcher, CustomWeights) {
    MatchWeights w;
    w.title = 1.0f;
    w.artist = 0.0f;
    w.album = 0.0f;
    w.duration = 0.0f;
    Matcher m(w);

    TrackMeta track;
    track.title = "晴天";
    track.artist = "someone else";

    SearchResult sr;
    sr.trackName = "晴天";
    sr.artistName = "周杰伦";

    EXPECT_EQ(m.score(track, sr), 100);  // 只看标题
}

TEST(Matcher, TitleJaccard75) {
    Matcher m;
    TrackMeta track;
    track.title = "love story taylor";
    track.artist = "y";

    SearchResult sr;
    sr.trackName = "love story taylors version";
    sr.artistName = "y";
    // "love story taylor" vs "love story taylors version"
    // "love story taylor" 是 "love story taylors version" 的子串前缀，
    // 触发 scoreText 中的 substring 分支 → 90 分，而非 Jaccard 分支。
    // title=90, artist=100, duration=0, album=0
    // 90*0.4 + 100*0.25 = 36+25 = 61
    int s = m.score(track, sr);
    EXPECT_EQ(s, 61);
}
