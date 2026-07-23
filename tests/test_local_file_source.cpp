#include <gtest/gtest.h>
#include "sources/LocalFileSource.h"
#include "ports/FileSystem.h"
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
    // 同 test_ports.cpp 的 FakeFs：从 seeded 文件 key 反推目录列表。
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

// --- Task 5 follow-up：目录扫描 + 大小写不敏感精确匹配 + 标准化标题模糊匹配 ---

TEST(LocalFileSource, CaseInsensitiveExactExtensionMatch) {
    FakeFs fs;
    // 真实文件系统区分大小写：目录里是 "Song.LRC"（大写扩展名），精确按候选名拼接
    // "Song.lrc" 去查找会落空，必须列目录后做大小写不敏感比较才能命中。
    fs.files["/music/album/Song.LRC"] = "[00:01.00]cased";
    LocalFileSource source(fs);
    TrackMeta track;
    track.path = "/music/album/Song.mp3";
    LyricData out;
    ASSERT_TRUE(source.fetch(track, out));
    EXPECT_TRUE(out.synced);
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "cased");
}

TEST(LocalFileSource, FuzzyMatchesAbbreviatedArtistFullTitleFile) {
    // 真实证据用例：ID3 title="i still carry on"，artist="MLTR"（缩写），
    // 但歌词文件名是 "Michael Learns To Rock - I Still Carry On.lrc"（全名+大小写不同），
    // 音轨 basename、<title>、<artist> - <title> 三种精确候选都不命中，只能靠模糊匹配
    // （标准化后标题子串包含）兜底。
    FakeFs fs;
    fs.files["/m/MLTR/Michael Learns To Rock - I Still Carry On.lrc"] = "[00:03.00]fuzzy hit";
    LocalFileSource source(fs);
    TrackMeta track;
    track.title = "i still carry on";
    track.artist = "MLTR";
    track.path = "/m/MLTR/MLTR- i still carry on.mp3";
    LyricData out;
    ASSERT_TRUE(source.fetch(track, out));
    EXPECT_TRUE(out.synced);
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "fuzzy hit");
}

TEST(LocalFileSource, FuzzyAmbiguityPrefersEntryContainingArtist) {
    // 两个候选都在标准化后包含标题，其中一个还包含艺术家，排序规则 (a) 优先选中它。
    FakeFs fs;
    fs.files["/m/x/Alan Walker - Faded.lrc"] = "[00:01.00]artist-match";
    fs.files["/m/x/DJ Faded Cover Mix.lrc"] = "[00:01.00]no-artist";
    LocalFileSource source(fs);
    TrackMeta track;
    track.title = "Faded";
    track.artist = "Alan Walker";
    track.path = "/m/x/track.mp3";
    LyricData out;
    ASSERT_TRUE(source.fetch(track, out));
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "artist-match");
}

TEST(LocalFileSource, FuzzyTieBreakIsDeterministicWithoutArtistMatch) {
    // 两个候选都不含艺术家，标准化长度相同，靠 (d) 字典序最小决出胜负。
    FakeFs fs;
    fs.files["/m/y/A Song.lrc"] = "[00:01.00]first";
    fs.files["/m/y/B Song.lrc"] = "[00:01.00]second";
    LocalFileSource source(fs);
    TrackMeta track;
    track.title = "Song";
    track.artist = "Zzz";  // 两个候选文件名都不含 "zzz"
    track.path = "/m/y/track.mp3";
    LyricData out;
    ASSERT_TRUE(source.fetch(track, out));
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "first");  // normalize("A Song.lrc") < normalize("B Song.lrc")
}

TEST(LocalFileSource, FuzzyTieBreakPrefersLrcOverTxtThenShorterName) {
    FakeFs fs;
    fs.files["/m/z/Faded Extended Remix.txt"] = "extended remix";
    fs.files["/m/z/Faded.lrc"] = "[00:01.00]short-lrc";
    LocalFileSource source(fs);
    TrackMeta track;
    track.title = "Faded";
    track.artist = "Nobody";
    track.path = "/m/z/track.mp3";
    LyricData out;
    ASSERT_TRUE(source.fetch(track, out));
    EXPECT_TRUE(out.synced);
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "short-lrc");  // .lrc 优先于 .txt，规则 (b)
}

TEST(LocalFileSource, NoFuzzyMatchWhenTitleNotContainedAnywhere) {
    FakeFs fs;
    fs.files["/m/w/Completely Unrelated Name.lrc"] = "[00:01.00]nope";
    LocalFileSource source(fs);
    TrackMeta track;
    track.title = "Faded";
    track.artist = "Alan Walker";
    track.path = "/m/w/track.mp3";
    LyricData out;
    EXPECT_FALSE(source.fetch(track, out));
}

// --- resolvePath：只定位命中文件的完整路径，不读内容（供"删除当前歌词文件"用）---

TEST(LocalFileSource, ResolvePathReturnsExactMatchFullPath) {
    FakeFs fs;
    fs.files["/music/album/01 - track.lrc"] = "[00:01.00]hi";
    LocalFileSource source(fs);
    TrackMeta track;
    track.path = "/music/album/01 - track.mp3";
    std::string path;
    ASSERT_TRUE(source.resolvePath(track, path));
    EXPECT_EQ(path, "/music/album/01 - track.lrc");
}

TEST(LocalFileSource, ResolvePathReturnsFuzzyMatchFullPath) {
    FakeFs fs;
    fs.files["/m/MLTR/Michael Learns To Rock - I Still Carry On.lrc"] = "[00:03.00]fuzzy hit";
    LocalFileSource source(fs);
    TrackMeta track;
    track.title = "i still carry on";
    track.artist = "MLTR";
    track.path = "/m/MLTR/MLTR- i still carry on.mp3";
    std::string path;
    ASSERT_TRUE(source.resolvePath(track, path));
    EXPECT_EQ(path, "/m/MLTR/Michael Learns To Rock - I Still Carry On.lrc");
}

TEST(LocalFileSource, ResolvePathMissReturnsFalse) {
    FakeFs fs;
    LocalFileSource source(fs);
    TrackMeta track;
    track.path = "/music/album/01 - track.mp3";
    std::string path;
    EXPECT_FALSE(source.resolvePath(track, path));
}

TEST(LocalFileSource, ResolvePathSucceedsEvenWhenFileEmpty) {
    // resolvePath 只做匹配、不看内容：空文件也应能定位到（fetch 才会因空内容判未命中）。
    FakeFs fs;
    fs.files["/music/album/01 - track.lrc"] = "";
    LocalFileSource source(fs);
    TrackMeta track;
    track.path = "/music/album/01 - track.mp3";
    std::string path;
    ASSERT_TRUE(source.resolvePath(track, path));
    EXPECT_EQ(path, "/music/album/01 - track.lrc");
    LyricData out;
    EXPECT_FALSE(source.fetch(track, out));
}

// --- stripExtension / normalize 单元测试 ---

TEST(LocalFileSource, StripExtensionNoExtension) {
    EXPECT_EQ(LocalFileSource::stripExtension("/music/album/track"), "/music/album/track");
}

TEST(LocalFileSource, StripExtensionSimple) {
    EXPECT_EQ(LocalFileSource::stripExtension("/music/album/track.mp3"), "/music/album/track");
}

TEST(LocalFileSource, StripExtensionMultipleDots) {
    // 只去掉最后一个 . 之后的部分
    EXPECT_EQ(LocalFileSource::stripExtension("/music/album/01. Song Title.mp3"),
              "/music/album/01. Song Title");
}

TEST(LocalFileSource, StripExtensionDirectoryOnlyPath) {
    // 文件名部分无 '.'，原样返回
    EXPECT_EQ(LocalFileSource::stripExtension("/music/album/"), "/music/album/");
}

TEST(LocalFileSource, StripExtensionHiddenFile) {
    // 最后一个 . 之后的部分被去掉，包括 .hidden 文件的 "扩展名"
    EXPECT_EQ(LocalFileSource::stripExtension("/music/.hidden"), "/music/");
}

TEST(LocalFileSource, NormalizeEmpty) {
    EXPECT_EQ(LocalFileSource::normalize(""), "");
}

TEST(LocalFileSource, NormalizeAlreadyClean) {
    EXPECT_EQ(LocalFileSource::normalize("hello123"), "hello123");
}

TEST(LocalFileSource, NormalizeUppercaseAndPunctuation) {
    EXPECT_EQ(LocalFileSource::normalize("Hello, World! 123"), "helloworld123");
}

TEST(LocalFileSource, NormalizeCjkOnly) {
    // CJK 字符保留
    EXPECT_EQ(LocalFileSource::normalize("后来"), "后来");
}

TEST(LocalFileSource, NormalizePunctuationOnlyBecomesEmpty) {
    EXPECT_EQ(LocalFileSource::normalize(" -.,!@#$%^&*() "), "");
}

TEST(LocalFileSource, NormalizeMixedScript) {
    EXPECT_EQ(LocalFileSource::normalize("Alan Walker - Faded (Remix)"), "alanwalkerfadedremix");
}

TEST(LocalFileSource, FuzzyMatchesChineseTitle) {
    // UTF-8 中文标题在标准化后应保留多字节字符，并可用于模糊匹配。
    // 文件名中包含中文标题，即使大小写/艺术家格式不同，也应通过标准化子串匹配命中。
    FakeFs fs;
    fs.files["/m/cn/歌手 - 后来.lrc"] = "[00:01.00]测试";
    LocalFileSource source(fs);
    TrackMeta track;
    track.title = "后来";
    track.artist = "歌手";
    track.path = "/m/cn/track.mp3";
    LyricData out;
    ASSERT_TRUE(source.fetch(track, out));
    EXPECT_TRUE(out.synced);
    ASSERT_EQ(out.lines.size(), 1u);
    EXPECT_EQ(out.lines[0].text, "测试");
}
