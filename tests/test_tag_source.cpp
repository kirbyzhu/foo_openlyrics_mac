#include <gtest/gtest.h>
#include "sources/TagSource.h"
#include "ports/TagIO.h"

using namespace openlyrics;

namespace {

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

}  // namespace

TEST(TagSource, TagPresentParsesToLyrics) {
    FakeTagIO tag;
    tag.has = true;
    tag.stored = "[00:01.00]hello\n[00:02.00]world";
    TagSource source(tag);
    TrackMeta track;
    LyricData out;
    ASSERT_TRUE(source.fetch(track, out));
    EXPECT_TRUE(out.synced);
    ASSERT_EQ(out.lines.size(), 2u);
    EXPECT_EQ(out.lines[0].text, "hello");
    EXPECT_EQ(out.lines[1].text, "world");
}

TEST(TagSource, TagAbsentReturnsFalse) {
    FakeTagIO tag;
    tag.has = false;
    TagSource source(tag);
    TrackMeta track;
    LyricData out;
    EXPECT_FALSE(source.fetch(track, out));
}

TEST(TagSource, EmptyTagStringReturnsFalse) {
    FakeTagIO tag;
    tag.has = true;
    tag.stored = "";
    TagSource source(tag);
    TrackMeta track;
    LyricData out;
    EXPECT_FALSE(source.fetch(track, out));
}
