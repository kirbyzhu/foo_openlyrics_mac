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

TEST(SyncEngine, StepForwardFromCurrentLine) {
    // 当前 pos=2000 落在 a(idx0)，+1 步 -> b(3000)，offset=0 -> seek 3000
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 2000, 1), 3000);
}

TEST(SyncEngine, StepBackwardFromCurrentLine) {
    // pos=4000 落在 b(idx1)，-1 步 -> a(1000)
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 4000, -1), 1000);
}

TEST(SyncEngine, StepClampsAtLastLine) {
    // pos=9000 落在 c(idx2，末行)，+1 步仍钳制到 c(5000)
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 9000, 1), 5000);
}

TEST(SyncEngine, StepClampsAtFirstLine) {
    // pos=2000 落在 a(idx0，首行)，-1 步钳制到 a(1000)
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 2000, -1), 1000);
}

TEST(SyncEngine, StepFromBeforeFirstLineForward) {
    // pos=500 在首句前(cur=-1)，+1 步 -> 首个时标行 a(1000)
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 500, 1), 1000);
}

TEST(SyncEngine, StepMultipleLines) {
    // pos=2000 落在 a(idx0)，+2 步 -> c(5000)
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 2000, 2), 5000);
}

TEST(SyncEngine, StepSkipsUntimedLines) {
    // 在 a 与 b 之间插入无时标行，步进应跳过它
    LyricData d;
    d.synced = true;
    d.lines.push_back({1000, "a", {}});
    d.lines.push_back({-1, "credit", {}});   // 无时标
    d.lines.push_back({3000, "b", {}});
    // pos=1500 落在 a，+1 步 -> b(3000)，而非无时标行
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(d, 1500, 1), 3000);
}

TEST(SyncEngine, StepAppliesOffsetInSeekTarget) {
    // offsetMs=-2000，pos=4000 -> eff=2000 落在 a(idx0)，+1 步 -> b(3000)
    // seek = 3000-(-2000)-0 = 5000
    LyricData d = makeData();
    d.offsetMs = -2000;
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(d, 4000, 1), 5000);
}

TEST(SyncEngine, StepSeekTargetClampsToZero) {
    // 目标 a.timeMs=1000，extraOffset=+5000 -> 1000-0-5000 = -4000 -> 钳制 0
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(makeData(), 4000, -1, 5000), 0);
}

TEST(SyncEngine, StepUnsyncedReturnsMinusOne) {
    LyricData d;
    d.synced = false;
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(d, 1000, 1), -1);
}

TEST(SyncEngine, StepNoTimedLinesReturnsMinusOne) {
    LyricData d;
    d.synced = true;
    d.lines.push_back({-1, "only credit", {}});
    EXPECT_EQ(SyncEngine::seekTargetForLineStep(d, 0, 1), -1);
}
