#include <gtest/gtest.h>
#include "sync/SyncEngine.h"

using namespace openlyrics;

static LyricData makeData() {
    LyricData d;
    d.synced = true;
    d.lines.push_back({1000, "a", {}});
    d.lines.push_back({3000, "b", {}});
    d.lines.push_back({5000, "c", {}});
    return d;
}

TEST(SyncEngine, BeforeFirstLine) {
    SyncResult r = SyncEngine::locate(makeData(), 500);
    EXPECT_EQ(r.lineIndex, -1);
    EXPECT_DOUBLE_EQ(r.progress, 0.0);
}

TEST(SyncEngine, ExactBoundaryIsCurrentLine) {
    SyncResult r = SyncEngine::locate(makeData(), 3000);
    EXPECT_EQ(r.lineIndex, 1);
    EXPECT_DOUBLE_EQ(r.progress, 0.0);
}

TEST(SyncEngine, MidwayProgress) {
    SyncResult r = SyncEngine::locate(makeData(), 2000);  // 在 a(1000) 与 b(3000) 之间
    EXPECT_EQ(r.lineIndex, 0);
    EXPECT_DOUBLE_EQ(r.progress, 0.5);
}

TEST(SyncEngine, LastLineProgressZero) {
    SyncResult r = SyncEngine::locate(makeData(), 9000);
    EXPECT_EQ(r.lineIndex, 2);
    EXPECT_DOUBLE_EQ(r.progress, 0.0);
}

TEST(SyncEngine, PositiveOffsetAdvances) {
    // extraOffset=+1000，eff=1500+1000=2500，落在 a(1000)
    SyncResult r = SyncEngine::locate(makeData(), 1500, 1000);
    EXPECT_EQ(r.lineIndex, 0);
    EXPECT_DOUBLE_EQ(r.progress, 0.75);
}

TEST(SyncEngine, DataOffsetApplied) {
    LyricData d = makeData();
    d.offsetMs = -2000;                       // eff = 3000 + (-2000) = 1000 -> a
    SyncResult r = SyncEngine::locate(d, 3000);
    EXPECT_EQ(r.lineIndex, 0);
}

TEST(SyncEngine, UnsyncedReturnsNoLine) {
    LyricData d;
    d.synced = false;
    d.lines.push_back({-1, "plain", {}});
    SyncResult r = SyncEngine::locate(d, 5000);
    EXPECT_EQ(r.lineIndex, -1);
}

TEST(SyncEngine, SkipsInterspersedUntimedLines) {
    LyricData d;
    d.synced = true;
    d.lines.push_back({1000, "a", {}});
    d.lines.push_back({-1, "note", {}});
    d.lines.push_back({3000, "b", {}});
    SyncResult r = SyncEngine::locate(d, 2000);
    EXPECT_EQ(r.lineIndex, 0);
    EXPECT_DOUBLE_EQ(r.progress, 0.5);  // 跳过 note 行，仍以 3000 为 next 做插值
}

TEST(SyncEngine, AdjacentForward) {
    EXPECT_EQ(SyncEngine::adjacentTimedLine(makeData(), 0, 1), 1);
}

TEST(SyncEngine, AdjacentBackward) {
    EXPECT_EQ(SyncEngine::adjacentTimedLine(makeData(), 1, -1), 0);
}

TEST(SyncEngine, AdjacentForwardStopsAtLast) {
    EXPECT_EQ(SyncEngine::adjacentTimedLine(makeData(), 2, 1), 2);
}

TEST(SyncEngine, AdjacentBackwardStopsAtFirst) {
    EXPECT_EQ(SyncEngine::adjacentTimedLine(makeData(), 0, -1), 0);
}

TEST(SyncEngine, AdjacentFromBeforeFirstForward) {
    EXPECT_EQ(SyncEngine::adjacentTimedLine(makeData(), -1, 1), 0);
}

TEST(SyncEngine, AdjacentFromBeforeFirstBackward) {
    EXPECT_EQ(SyncEngine::adjacentTimedLine(makeData(), -1, -1), -1);
}

TEST(SyncEngine, AdjacentSkipsUntimedForward) {
    LyricData d;
    d.synced = true;
    d.lines.push_back({1000, "a", {}});
    d.lines.push_back({-1, "note", {}});
    d.lines.push_back({3000, "b", {}});
    EXPECT_EQ(SyncEngine::adjacentTimedLine(d, 0, 1), 2);   // 跳过 note
}

TEST(SyncEngine, AdjacentSkipsUntimedBackward) {
    LyricData d;
    d.synced = true;
    d.lines.push_back({1000, "a", {}});
    d.lines.push_back({-1, "note", {}});
    d.lines.push_back({3000, "b", {}});
    EXPECT_EQ(SyncEngine::adjacentTimedLine(d, 2, -1), 0);  // 跳过 note
}

TEST(SyncEngine, AdjacentNoTimedLines) {
    LyricData d;
    d.synced = true;
    d.lines.push_back({-1, "only note", {}});
    EXPECT_EQ(SyncEngine::adjacentTimedLine(d, -1, 1), -1);
}

TEST(SyncEngine, SyllableLocateAtFirstSyllable) {
    LyricData d;
    d.synced = true;
    LyricLine line;
    line.timeMs = 1000;
    line.text = "abc";
    line.syllables = {
        {1000, 1500, "a"},
        {1500, 2000, "b"},
        {2000, 2500, "c"}
    };
    d.lines.push_back(line);

    SyncResult r = SyncEngine::locate(d, 1250);
    EXPECT_EQ(r.lineIndex, 0);
    EXPECT_EQ(r.syllableIndex, 0);
    EXPECT_DOUBLE_EQ(r.syllableProgress, 0.5);
}

TEST(SyncEngine, SyllableLocateAtMiddleSyllable) {
    LyricData d;
    d.synced = true;
    LyricLine line;
    line.timeMs = 1000;
    line.text = "abc";
    line.syllables = {
        {1000, 1500, "a"},
        {1500, 2000, "b"},
        {2000, 2500, "c"}
    };
    d.lines.push_back(line);

    SyncResult r = SyncEngine::locate(d, 1750);
    EXPECT_EQ(r.lineIndex, 0);
    EXPECT_EQ(r.syllableIndex, 1);
    EXPECT_DOUBLE_EQ(r.syllableProgress, 0.5);
}

TEST(SyncEngine, SyllableLocateFallbackEndMsToNextStart) {
    LyricData d;
    d.synced = true;
    LyricLine line;
    line.timeMs = 1000;
    line.text = "ab";
    line.syllables = {
        {1000, 0, "a"},  // endMs == 0
        {1500, 0, "b"}
    };
    d.lines.push_back(line);

    SyncResult r = SyncEngine::locate(d, 1250);
    EXPECT_EQ(r.lineIndex, 0);
    EXPECT_EQ(r.syllableIndex, 0);
    EXPECT_DOUBLE_EQ(r.syllableProgress, 0.5);  // fallback endMs = next start (1500)
}

TEST(SyncEngine, NoSyllablesFallbackToLineMode) {
    LyricData d = makeData();
    SyncResult r = SyncEngine::locate(d, 2000);
    EXPECT_EQ(r.lineIndex, 0);
    EXPECT_EQ(r.syllableIndex, -1);
}

