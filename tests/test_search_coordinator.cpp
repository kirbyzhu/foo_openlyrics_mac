#include <gtest/gtest.h>
#include "pipeline/SearchCoordinator.h"
#include "sources/TagSource.h"
#include "sources/LocalFileSource.h"
#include "ports/HttpClient.h"
#include "ports/FileSystem.h"
#include "ports/TagIO.h"
#include <map>

using namespace openlyrics;

namespace {

// --- Fake 组件 ---

class FakeTagIO : public TagIO {
public:
    std::string stored;
    bool has = false;
    bool readLyricTag(const TrackMeta&, std::string& out) override {
        if (!has) return false;
        out = stored;
        return true;
    }
    bool writeLyricTag(const TrackMeta&, const std::string& lrc) override {
        stored = lrc;
        has = true;
        return true;
    }
};

class FakeFs : public FileSystem {
public:
    std::map<std::string, std::string> files;
    bool readFile(const std::string& p, std::string& out) override {
        auto it = files.find(p);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }
    bool writeFile(const std::string& p, const std::string& d) override {
        files[p] = d;
        return true;
    }
    std::vector<std::string> listDirectory(const std::string& dir) override {
        std::vector<std::string> result;
        for (const auto& kv : files) {
            const std::string& path = kv.first;
            const size_t slash = path.find_last_of('/');
            const std::string parent = (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);
            if (parent == dir) result.push_back(path.substr(slash + 1));
        }
        return result;
    }
};

// Fake 在线源，可预设 search 和 fetchById 的行为
class FakeOnlineSource : public LyricSource {
public:
    std::vector<SearchResult> searchResults;
    bool searchOk = true;
    LyricData lyricData;
    bool fetchByIdOk = true;
    SourceId sid = SourceId::Unknown;

    FakeOnlineSource(SourceId id) : sid(id) {}

    bool search(const TrackMeta&, std::vector<SearchResult>& out) override {
        if (!searchOk) return false;
        for (auto& r : searchResults) {
            r.source = sid;
        }
        out = searchResults;
        return !out.empty();
    }
    bool fetchById(const std::string&, LyricData& out) override {
        if (!fetchByIdOk) return false;
        out = lyricData;
        return true;
    }
    SourceId sourceId() const override { return sid; }
};

SearchResult makeCandidate(const std::string& id, const std::string& title,
                           const std::string& artist, int durSec = 0) {
    SearchResult sr;
    sr.id = id;
    sr.trackName = title;
    sr.artistName = artist;
    sr.durationSec = durSec;
    return sr;
}

}  // namespace

// --- 测试用例 ---

// 本地快速通道命中 → 直接返回，不调在线源
TEST(SearchCoordinator, LocalHitSkipsOnline) {
    FakeTagIO tagIO;
    tagIO.has = true;
    tagIO.stored = "[00:01.00]local lyric\n[00:02.00]test";
    TagSource tagSource(tagIO);

    FakeFs fs;
    LocalFileSource localSource(fs);
    SearchPipeline pipeline({&tagSource, &localSource});

    FakeOnlineSource fakeOnline(SourceId::LrcLib);
    fakeOnline.searchResults = {makeCandidate("1", "x", "y")};
    LyricLine l; l.timeMs = 1000; l.text = "online";
    fakeOnline.lyricData.lines = {l};

    Matcher matcher;
    SearchCoordinator coordinator(&pipeline, {&fakeOnline}, matcher);

    TrackMeta track;
    track.artist = "x";
    track.title = "y";

    LyricData out;
    ASSERT_TRUE(coordinator.resolve(track, out));
    // 应该命中 tag source，不是 online
    EXPECT_EQ(out.lines[0].text, "local lyric");
}

// 本地未命中 → 在线候选评分取最优
TEST(SearchCoordinator, OnlineFallbackBestCandidate) {
    FakeTagIO tagIO;
    FakeFs fs;
    TagSource tagSource(tagIO);
    LocalFileSource localSource(fs);
    SearchPipeline pipeline({&tagSource, &localSource});

    FakeOnlineSource fakeOnline(SourceId::NetEase);
    // 两条候选：第一条低分，第二条高分（精确匹配）
    fakeOnline.searchResults = {
        makeCandidate("wrong", "Wrong Title", "Wrong Artist", 100),
        makeCandidate("correct", "晴天", "周杰伦", 269),
    };

    LyricLine l; l.timeMs = 1000; l.text = "correct lyric";
    fakeOnline.lyricData.lines = {l};

    Matcher matcher;
    SearchCoordinator coordinator(&pipeline, {&fakeOnline}, matcher);

    TrackMeta track;
    track.title = "晴天";
    track.artist = "周杰伦";
    track.lengthMs = 269000;

    LyricData out;
    ASSERT_TRUE(coordinator.resolve(track, out));
    // 应该选中高分的第二条候选
    EXPECT_EQ(out.lines[0].text, "correct lyric");
}

// 在线最高分低于阈值 → 返回 false
TEST(SearchCoordinator, LowScoreReturnsFalse) {
    FakeTagIO tagIO;
    FakeFs fs;
    TagSource tagSource(tagIO);
    LocalFileSource localSource(fs);
    SearchPipeline pipeline({&tagSource, &localSource});

    FakeOnlineSource fakeOnline(SourceId::QQMusic);
    fakeOnline.searchResults = {
        makeCandidate("bad", "完全不相关的标题", "不相关的艺术家", 999),
    };

    Matcher matcher;
    SearchCoordinator coordinator(&pipeline, {&fakeOnline}, matcher);

    TrackMeta track;
    track.title = "晴天";
    track.artist = "周杰伦";

    LyricData out;
    EXPECT_FALSE(coordinator.resolve(track, out));
}

// searchAll 按源分组 + 组内降序
TEST(SearchCoordinator, SearchAllGroupsBySource) {
    FakeTagIO tagIO;
    FakeFs fs;
    TagSource tagSource(tagIO);
    LocalFileSource localSource(fs);
    SearchPipeline pipeline({&tagSource, &localSource});

    FakeOnlineSource netease(SourceId::NetEase);
    netease.searchResults = {
        makeCandidate("ne1", "晴天", "周杰伦", 269),
    };

    FakeOnlineSource qq(SourceId::QQMusic);
    qq.searchResults = {
        makeCandidate("qq1", "晴天 (Live)", "周杰伦", 280),
        makeCandidate("qq2", "晴天", "周杰伦", 269),
    };

    Matcher matcher;
    SearchCoordinator coordinator(&pipeline, {&netease, &qq}, matcher);

    TrackMeta track;
    track.title = "晴天";
    track.artist = "周杰伦";
    track.lengthMs = 269000;

    auto groups = coordinator.searchAll(track);
    ASSERT_EQ(groups.size(), 2u);

    // 每组内按分数降序
    for (const auto& g : groups) {
        for (size_t i = 1; i < g.items.size(); ++i) {
            EXPECT_GE(g.items[i-1].score, g.items[i].score);
        }
    }
}

// 某在线源失败 → 不影响其他源
TEST(SearchCoordinator, FailingSourceDoesNotAffectOthers) {
    FakeTagIO tagIO;
    FakeFs fs;
    TagSource tagSource(tagIO);
    LocalFileSource localSource(fs);
    SearchPipeline pipeline({&tagSource, &localSource});

    FakeOnlineSource bad(SourceId::NetEase);
    bad.searchOk = false;  // 总是失败

    FakeOnlineSource good(SourceId::LrcLib);
    good.searchResults = {
        makeCandidate("lr1", "晴天", "周杰伦", 269),
    };

    Matcher matcher;
    SearchCoordinator coordinator(&pipeline, {&bad, &good}, matcher);

    TrackMeta track;
    track.title = "晴天";
    track.artist = "周杰伦";
    track.lengthMs = 269000;

    LyricData out;
    // 应该通过 good 源成功
    EXPECT_TRUE(coordinator.resolve(track, out));
}
