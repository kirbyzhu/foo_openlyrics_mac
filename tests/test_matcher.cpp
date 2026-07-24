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

// --- 变体标记惩罚：不同录音/不同歌词的候选不应靠子串拿高分 ---

TEST(Matcher, ForeignLanguageVariantPenalized) {
    MatchWeights w; w.title = 1.0f; w.artist = 0.0f; w.album = 0.0f; w.duration = 0.0f;
    Matcher m(w);
    TrackMeta track; track.title = "I Just Can't Stop Loving You";
    SearchResult sr;
    sr.trackName = "I Just Can't Stop Loving You(Todo mi amor eres tu)(Spanish version)";
    // 西班牙语版歌词完全不同，标题分应被压低（曾走子串分支得 90）
    EXPECT_LE(m.score(track, sr), 20);
}

TEST(Matcher, LiveVariantPenalized) {
    MatchWeights w; w.title = 1.0f; w.artist = 0.0f; w.album = 0.0f; w.duration = 0.0f;
    Matcher m(w);
    TrackMeta track; track.title = "I Just Can't Stop Loving You";
    SearchResult sr; sr.trackName = "I Just Can't Stop Loving You (Live)";
    EXPECT_LE(m.score(track, sr), 20);
}

TEST(Matcher, SpokenIntroVariantPenalized) {
    MatchWeights w; w.title = 1.0f; w.artist = 0.0f; w.album = 0.0f; w.duration = 0.0f;
    Matcher m(w);
    TrackMeta track; track.title = "I Just Can't Stop Loving You";
    SearchResult sr; sr.trackName = "I Just Can't Stop Loving You (With Spoken Intro)";
    // 带开场对白的版本歌词不同（多出对白行），应被压低
    EXPECT_LE(m.score(track, sr), 20);
}

TEST(Matcher, RemasterVariantNotPenalized) {
    MatchWeights w; w.title = 1.0f; w.artist = 0.0f; w.album = 0.0f; w.duration = 0.0f;
    Matcher m(w);
    TrackMeta track; track.title = "Billie Jean";
    SearchResult sr; sr.trackName = "Billie Jean (2012 Remaster)";
    // remaster 歌词相同，仍应走子串高分
    EXPECT_EQ(m.score(track, sr), 90);
}

TEST(Matcher, VariantMarkerInQueryNotPenalized) {
    // 用户文件本身就是现场版 → 候选现场版属精确匹配，不应被罚
    MatchWeights w; w.title = 1.0f; w.artist = 0.0f; w.album = 0.0f; w.duration = 0.0f;
    Matcher m(w);
    TrackMeta track; track.title = "Hotel California (Live)";
    SearchResult sr; sr.trackName = "Hotel California (Live)";
    EXPECT_EQ(m.score(track, sr), 100);
}

// --- CJK bigram 相似度 ---

TEST(JaccardSimilarity, CjkIdentical) {
    EXPECT_DOUBLE_EQ(jaccardSimilarity("晴天", "晴天"), 1.0);
}

TEST(JaccardSimilarity, CjkPartialOverlap) {
    // "第一天" bigram {第一,一天}; "每一天" bigram {每一,一天}
    // 交集 {一天}=1, 并集 {第一,一天,每一}=3 -> 1/3
    EXPECT_NEAR(jaccardSimilarity("第一天", "每一天"), 1.0/3.0, 0.01);
}

TEST(JaccardSimilarity, CjkNoOverlap) {
    // "晴天" {晴天} vs "稻香" {稻香} -> 0
    EXPECT_DOUBLE_EQ(jaccardSimilarity("晴天", "稻香"), 0.0);
}

TEST(JaccardSimilarity, CjkSingleChar) {
    // 单字符段用单字 token："火" vs "火" -> 1.0
    EXPECT_DOUBLE_EQ(jaccardSimilarity("火", "火"), 1.0);
}

TEST(JaccardSimilarity, MixedCjkAscii) {
    // "陈奕迅 eason" -> CJK 段 {陈奕,奕迅} + ASCII 词 {eason}
    // 与自身 -> 1.0
    EXPECT_DOUBLE_EQ(jaccardSimilarity("陈奕迅 eason", "陈奕迅 eason"), 1.0);
}

// --- normalizeQuery ---

TEST(NormalizeQuery, StripsParens) {
    EXPECT_EQ(normalizeQuery("Love Story (Taylor's Version)"), "Love Story");
}

TEST(NormalizeQuery, StripsFullWidthParens) {
    // 中文全角括号（电影主题曲）
    EXPECT_EQ(normalizeQuery("情非得已\xEF\xBC\x88电影主题曲\xEF\xBC\x89"), "情非得已");
}

TEST(NormalizeQuery, StripsBrackets) {
    EXPECT_EQ(normalizeQuery("告白气球\xE3\x80\x90Live\xE3\x80\x91"), "告白气球");
}

TEST(NormalizeQuery, StripsFeat) {
    EXPECT_EQ(normalizeQuery("Song feat. Artist B"), "Song");
}

TEST(NormalizeQuery, PlainTitleUnchanged) {
    EXPECT_EQ(normalizeQuery("晴天"), "晴天");
}

TEST(NormalizeQuery, AllInParensFallbackToOriginal) {
    // 清理后为空则回退原串
    EXPECT_EQ(normalizeQuery("(instrumental)"), "(instrumental)");
}
