#include <gtest/gtest.h>
#include "ports/HttpClient.h"
#include "ports/FileSystem.h"
#include "ports/TagIO.h"
#include "ports/Clock.h"
#include <map>

using namespace openlyrics;

namespace {

class FakeHttp : public HttpClient {
public:
    HttpResponse get(const std::string& url,
                     const std::vector<std::pair<std::string, std::string>>& = {}) override {
        HttpResponse r;
        r.status = 200;
        r.body = "ok:" + url;
        return r;
    }
};

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
    EXPECT_FALSE(fs.exists("a.lrc"));
    EXPECT_TRUE(fs.writeFile("a.lrc", "data"));
    EXPECT_TRUE(fs.exists("a.lrc"));
    std::string out;
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
