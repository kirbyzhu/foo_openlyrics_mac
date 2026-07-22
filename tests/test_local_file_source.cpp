#include <gtest/gtest.h>
#include "sources/LocalFileSource.h"
#include "ports/FileSystem.h"
#include <map>

using namespace openlyrics;

namespace {

class FakeFs : public FileSystem {
public:
    std::map<std::string, std::string> files;
    bool exists(const std::string& p) override { return files.count(p) > 0; }
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
};

}  // namespace

TEST(LocalFileSource, FindsLrcFile) {
    FakeFs fs;
    fs.files["/music/album/01 - track.lrc"] = "[00:01.00]hi";
    LocalFileSource source(fs);
    TrackMeta track;
    track.path = "/music/album/01 - track.mp3";
    LyricData out;
    ASSERT_TRUE(source.fetch(track, out));
    EXPECT_TRUE(out.synced);
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "hi");
}

TEST(LocalFileSource, FallsBackToTxtWhenNoLrc) {
    FakeFs fs;
    fs.files["/music/album/01 - track.txt"] = "plain lyric line";
    LocalFileSource source(fs);
    TrackMeta track;
    track.path = "/music/album/01 - track.mp3";
    LyricData out;
    ASSERT_TRUE(source.fetch(track, out));
    EXPECT_FALSE(out.synced);
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "plain lyric line");
}

TEST(LocalFileSource, NeitherFoundReturnsFalse) {
    FakeFs fs;
    LocalFileSource source(fs);
    TrackMeta track;
    track.path = "/music/album/01 - track.mp3";
    LyricData out;
    EXPECT_FALSE(source.fetch(track, out));
}

TEST(LocalFileSource, PathWithNoExtensionAppendsSuffix) {
    FakeFs fs;
    fs.files["/music/album/track.lrc"] = "[00:01.00]no ext";
    LocalFileSource source(fs);
    TrackMeta track;
    track.path = "/music/album/track";
    LyricData out;
    ASSERT_TRUE(source.fetch(track, out));
    EXPECT_EQ(out.lines[0].text, "no ext");
}

TEST(LocalFileSource, BasenameWithSpacesResolvesCorrectly) {
    FakeFs fs;
    fs.files["/music/My Album/01 - My Song Title.lrc"] = "[00:01.00]spaced";
    LocalFileSource source(fs);
    TrackMeta track;
    track.path = "/music/My Album/01 - My Song Title.mp3";
    LyricData out;
    ASSERT_TRUE(source.fetch(track, out));
    EXPECT_EQ(out.lines[0].text, "spaced");
}

TEST(LocalFileSource, EmptyFileContentTreatedAsMiss) {
    FakeFs fs;
    fs.files["/music/album/01 - track.lrc"] = "";
    LocalFileSource source(fs);
    TrackMeta track;
    track.path = "/music/album/01 - track.mp3";
    LyricData out;
    EXPECT_FALSE(source.fetch(track, out));
}

TEST(LocalFileSource, PrefersLrcOverTxtWhenBothExist) {
    FakeFs fs;
    fs.files["/music/album/01 - track.lrc"] = "[00:01.00]from-lrc";
    fs.files["/music/album/01 - track.txt"] = "from-txt";
    LocalFileSource source(fs);
    TrackMeta track;
    track.path = "/music/album/01 - track.mp3";
    LyricData out;
    ASSERT_TRUE(source.fetch(track, out));
    EXPECT_TRUE(out.synced);
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "from-lrc");
}

TEST(LocalFileSource, ParentDirDotNotMistakenForExtension) {
    FakeFs fs;
    fs.files["/music/R.E.M/01 - Song.lrc"] = "[00:02.00]ok";
    LocalFileSource source(fs);
    TrackMeta track;
    track.path = "/music/R.E.M/01 - Song.mp3";
    LyricData out;
    ASSERT_TRUE(source.fetch(track, out));
    EXPECT_TRUE(out.synced);
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "ok");
}
