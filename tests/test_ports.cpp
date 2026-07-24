#include <gtest/gtest.h>
#include "ports/HttpClient.h"
#include "ports/FileSystem.h"
#include "ports/TagIO.h"
#include "ports/Clock.h"
#include <algorithm>
#include <map>

using namespace openlyrics;

namespace {

class FakeHttp : public HttpClient {
public:
    HttpResponse get(const std::string& url,
                     const std::vector<std::pair<std::string, std::string>>& = {},
                     CancelToken* = nullptr) override {
        HttpResponse r;
        r.status = 200;
        r.body = "ok:" + url;
        return r;
    }
    HttpResponse post(const std::string& url,
                       const std::string&,
                       const std::vector<std::pair<std::string, std::string>>& = {},
                       CancelToken* = nullptr) override {
        HttpResponse r;
        r.status = 200;
        r.body = "post:" + url;
        return r;
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
    // 从 files 的 key 反推目录列表：key 的父目录（最后一个 '/' 含分隔符本身）等于 dir
    // 的，取其 basename 收进结果；dir 下没有任何 seeded 文件时天然返回空 vector。
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

class FakeClock : public Clock {
public:
    int64_t t = 42;
    int64_t nowMs() override { return t; }
};

}  // namespace

TEST(Ports, HttpFakeReturnsBody) {
    FakeHttp h;
    HttpResponse r = h.get("http://x");
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.body, "ok:http://x");
}

TEST(Ports, FileSystemRoundTrip) {
    FakeFs fs;
    std::string out;
    EXPECT_FALSE(fs.readFile("a.lrc", out));
    EXPECT_TRUE(fs.writeFile("a.lrc", "data"));
    EXPECT_TRUE(fs.readFile("a.lrc", out));
    EXPECT_EQ(out, "data");
}

TEST(Ports, TagIoRoundTrip) {
    FakeTagIO tag;
    TrackMeta t;
    std::string out;
    EXPECT_FALSE(tag.readLyricTag(t, out));
    EXPECT_TRUE(tag.writeLyricTag(t, "[00:00.00]x"));
    EXPECT_TRUE(tag.readLyricTag(t, out));
    EXPECT_EQ(out, "[00:00.00]x");
}

TEST(Ports, ClockReadsTime) {
    FakeClock c;
    EXPECT_EQ(c.nowMs(), 42);
}

TEST(Ports, FileSystemListDirectoryReturnsBasenames) {
    FakeFs fs;
    fs.files["/music/album/a.lrc"] = "x";
    fs.files["/music/album/b.txt"] = "y";
    fs.files["/music/other/c.lrc"] = "z";
    std::vector<std::string> entries = fs.listDirectory("/music/album/");
    std::sort(entries.begin(), entries.end());
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0], "a.lrc");
    EXPECT_EQ(entries[1], "b.txt");
}

TEST(Ports, FileSystemListDirectoryMissingDirReturnsEmpty) {
    FakeFs fs;
    fs.files["/music/album/a.lrc"] = "x";
    EXPECT_TRUE(fs.listDirectory("/music/missing/").empty());
}
