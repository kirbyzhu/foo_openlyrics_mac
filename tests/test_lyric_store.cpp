#include <gtest/gtest.h>
#include "store/LyricStore.h"
#include "ports/FileSystem.h"
#include "model/TrackMeta.h"
#include "model/LyricData.h"
#include <map>

using namespace openlyrics;

namespace {

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
            if (parent == dir) {
                result.push_back(path.substr(slash + 1));
            }
        }
        return result;
    }
};

class FailingFs : public FileSystem {
public:
    bool readFile(const std::string&, std::string&) override { return false; }
    bool writeFile(const std::string&, const std::string&) override { return false; }
    std::vector<std::string> listDirectory(const std::string&) override { return {}; }
};

}  // namespace

TEST(LyricStore, SaveNormalTrackWritesToLrcFile) {
    FakeFs fs;
    LyricStore store(fs);

    TrackMeta track;
    track.path = "/m/Song.mp3";

    LyricData data;
    data.sourceText = "[00:01.00]hi";

    bool result = store.save(track, data);

    EXPECT_TRUE(result);
    EXPECT_TRUE(fs.files.count("/m/Song.lrc"));
    EXPECT_EQ(fs.files["/m/Song.lrc"], "[00:01.00]hi");
}

TEST(LyricStore, SaveEmptySourceTextReturnsFalse) {
    FakeFs fs;
    LyricStore store(fs);

    TrackMeta track;
    track.path = "/m/Song.mp3";

    LyricData data;
    data.sourceText = "";

    bool result = store.save(track, data);

    EXPECT_FALSE(result);
    EXPECT_FALSE(fs.files.count("/m/Song.lrc"));
}

TEST(LyricStore, SavePathWithoutExtension) {
    FakeFs fs;
    LyricStore store(fs);

    TrackMeta track;
    track.path = "/m/Song";

    LyricData data;
    data.sourceText = "[00:01.00]test";

    bool result = store.save(track, data);

    EXPECT_TRUE(result);
    EXPECT_TRUE(fs.files.count("/m/Song.lrc"));
    EXPECT_EQ(fs.files["/m/Song.lrc"], "[00:01.00]test");
}

TEST(LyricStore, SavePathWithDotInParentDir) {
    FakeFs fs;
    LyricStore store(fs);

    TrackMeta track;
    track.path = "/m/R.E.M/Song.mp3";

    LyricData data;
    data.sourceText = "[00:02.00]rem";

    bool result = store.save(track, data);

    EXPECT_TRUE(result);
    EXPECT_TRUE(fs.files.count("/m/R.E.M/Song.lrc"));
    EXPECT_EQ(fs.files["/m/R.E.M/Song.lrc"], "[00:02.00]rem");
}

TEST(LyricStore, SaveWriteFileFailurePropagates) {
    FailingFs fs;
    LyricStore store(fs);

    TrackMeta track;
    track.path = "/m/Song.mp3";

    LyricData data;
    data.sourceText = "[00:01.00]hi";

    bool result = store.save(track, data);

    EXPECT_FALSE(result);
}

TEST(LyricStore, DoesNotOverwriteExistingLrc) {
    FakeFs fs;
    fs.files["/m/Song.lrc"] = "[00:00.00]existing user lyric";
    LyricStore store(fs);

    TrackMeta track;
    track.path = "/m/Song.mp3";

    LyricData data;
    data.sourceText = "[00:01.00]fetched online";

    bool result = store.save(track, data);

    EXPECT_FALSE(result);
    ASSERT_TRUE(fs.files.count("/m/Song.lrc"));
    EXPECT_EQ(fs.files["/m/Song.lrc"], "[00:00.00]existing user lyric");
}

TEST(LyricStore, ExistingCaseInsensitiveAlsoBlocks) {
    FakeFs fs;
    fs.files["/m/Song.LRC"] = "[00:00.00]existing user lyric";
    LyricStore store(fs);

    TrackMeta track;
    track.path = "/m/Song.mp3";

    LyricData data;
    data.sourceText = "[00:01.00]fetched online";

    bool result = store.save(track, data);

    EXPECT_FALSE(result);
    ASSERT_TRUE(fs.files.count("/m/Song.LRC"));
    EXPECT_EQ(fs.files["/m/Song.LRC"], "[00:00.00]existing user lyric");
    EXPECT_FALSE(fs.files.count("/m/Song.lrc"));
}
