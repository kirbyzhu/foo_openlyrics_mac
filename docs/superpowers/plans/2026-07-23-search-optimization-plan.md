# 歌词搜索优化 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 参考 LDDC 搜索设计，补齐 NetEase/QQMusic 手动搜索能力 + 引入多候选评分机制提升自动取词命中率。

**Architecture:** 新增 SearchCoordinator 编排层统一两模式路径（收集候选池→Matcher 评分→按模式分发），LyricSource 接口扩展 search/fetchById，NetEase/QQMusic Provider 拆分搜索与取词逻辑。不改 SearchPipeline/TagSource/LocalFileSource/LrcParser/LyricStore/SyncEngine。

**Tech Stack:** C++17, Google Test, Objective-C++ (UI 层), CMake 3.20+, foobar2000 SDK 2025-03-07

**Design Spec:** `docs/superpowers/specs/2026-07-23-search-optimization-design.md`

## Global Constraints

- 不改动的文件：`SearchPipeline.h/.cpp`、`TagSource.cpp`、`LocalFileSource.cpp`、`LrcParser.h/.cpp`、`LyricStore.h/.cpp`、`SyncEngine.h/.cpp`
- 核心层纯 C++17，零平台依赖，命名空间 `openlyrics`
- 单元测试用 Google Test，fake/mock 在测试文件中就地定义
- 构建：`cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`
- 当前测试基线 138/138 通过

---

### Task 1: SearchResult + LyricSource 接口扩展

**Files:**
- Modify: `extensions/foo_openlyrics_mac/core/model/SearchResult.h`
- Modify: `extensions/foo_openlyrics_mac/core/sources/LyricSource.h`
- Modify: `extensions/foo_openlyrics_mac/core/sources/TagSource.h`
- Modify: `extensions/foo_openlyrics_mac/core/sources/LocalFileSource.h`
- Modify: `extensions/foo_openlyrics_mac/core/sources/LrcLibProvider.h`

**Interfaces:**
- Produces: `SourceId` 枚举、`SearchResult::id/source/score`、`sourceDisplayName()`、`LyricSource::search()/fetchById()/fetch()/sourceId()`

- [ ] **Step 1: 扩展 SearchResult.h**

在 `SearchResult.h` 中，在现有 `struct SearchResult` 之前添加 `SourceId` 枚举和 `sourceDisplayName()` 函数，在结构体内添加新字段。文件最终内容：

```cpp
#pragma once
#include <cstdint>
#include <string>

namespace openlyrics {

enum class SourceId { Unknown, Tag, Local, LrcLib, NetEase, QQMusic };

inline const char* sourceDisplayName(SourceId s) {
    switch (s) {
        case SourceId::LrcLib:   return "LrcLib";
        case SourceId::NetEase:  return "网易云音乐";
        case SourceId::QQMusic:  return "QQ 音乐";
        case SourceId::Local:    return "本地文件";
        case SourceId::Tag:      return "内嵌标签";
        default:                 return "未知";
    }
}

struct SearchResult {
    std::string id;                    // 新增：fetchById 用的标识符
    std::string trackName;
    std::string artistName;
    std::string albumName;
    int durationSec = 0;
    SourceId source = SourceId::Unknown;  // 新增
    int score = 0;                       // 新增：Matcher 评分 0-100
};

}  // namespace openlyrics
```

- [ ] **Step 2: 扩展 LyricSource.h**

在 `LyricSource.h` 中，添加 `#include "model/SearchResult.h"` 和 `<vector>`，将 `fetch()` 从纯虚改为有默认实现的虚方法，新增 `search()`、`fetchById()`、`sourceId()`。文件最终内容：

```cpp
#pragma once
#include "model/LyricData.h"
#include "model/TrackMeta.h"
#include "model/SearchResult.h"
#include <vector>

namespace openlyrics {

class LyricSource {
public:
    virtual ~LyricSource() = default;

    // 搜索候选列表。默认返回 false。
    virtual bool search(const TrackMeta& track, std::vector<SearchResult>& out) {
        (void)track; (void)out;
        return false;
    }

    // 按 ID 拉取歌词。默认返回 false。
    virtual bool fetchById(const std::string& id, LyricData& out) {
        (void)id; (void)out;
        return false;
    }

    // 自动模式一键取词。默认实现：search 取第一候选 → fetchById。
    // TagSource / LocalFileSource 覆写此方法提供自己的取词逻辑。
    virtual bool fetch(const TrackMeta& track, LyricData& out) {
        std::vector<SearchResult> results;
        if (!search(track, results) || results.empty()) return false;
        return fetchById(results[0].id, out);
    }

    // 返回此源的类型标识。默认 Unknown。
    virtual SourceId sourceId() const { return SourceId::Unknown; }
};

}  // namespace openlyrics
```

- [ ] **Step 3: TagSource.h 添加 sourceId() 覆写**

在 `TagSource.h` 的 public 区域，`fetch()` 声明之后添加：

```cpp
    SourceId sourceId() const override { return SourceId::Tag; }
```

文件最终内容：

```cpp
#pragma once
#include "sources/LyricSource.h"
#include "ports/TagIO.h"
#include "parser/LrcParser.h"

namespace openlyrics {

// 从内嵌标签读取歌词的源
class TagSource : public LyricSource {
public:
    explicit TagSource(TagIO& tagio);
    bool fetch(const TrackMeta& track, LyricData& out) override;
    SourceId sourceId() const override { return SourceId::Tag; }

private:
    TagIO& tagio_;
};

}  // namespace openlyrics
```

- [ ] **Step 4: LocalFileSource.h 添加 sourceId() 覆写**

在 `LocalFileSource.h` 的 public 区域，`fetch()` 声明之后添加：

```cpp
    SourceId sourceId() const override { return SourceId::Local; }
```

> 注意：文件其余部分（`resolvePath`、`stripExtension`、`normalize` 等）保持不变。

- [ ] **Step 5: LrcLibProvider.h 添加新 override 声明**

LrcLibProvider 现有 `search(const std::string& query, ...)` 和 `fetchById(int id, ...)` 签名与 LyricSource 基类不同，需新增签名匹配的 override 方法。同时添加 `sourceId()` 覆写。

在 `LrcLibProvider.h` 的 public 区域，`fetch()` 声明之后、`private:` 之前添加：

```cpp
    // LyricSource 接口适配：将 TrackMeta 转为查询串委托现有 search()
    bool search(const TrackMeta& track, std::vector<SearchResult>& out) override;
    // LyricSource 接口适配：将字符串 id 转 int 委托现有 fetchById()
    bool fetchById(const std::string& id, LyricData& out) override;
    SourceId sourceId() const override { return SourceId::LrcLib; }
```

文件最终内容：

```cpp
#pragma once
#include "sources/LyricSource.h"
#include "model/SearchResult.h"
#include "ports/HttpClient.h"
#include <vector>

namespace openlyrics {

class LrcLibProvider : public LyricSource {
public:
    explicit LrcLibProvider(HttpClient& http);
    bool fetch(const TrackMeta& track, LyricData& out) override;

    bool search(const std::string& query, std::vector<SearchResult>& out);
    bool fetchById(int id, LyricData& out);

    // LyricSource 接口适配
    bool search(const TrackMeta& track, std::vector<SearchResult>& out) override;
    bool fetchById(const std::string& id, LyricData& out) override;
    SourceId sourceId() const override { return SourceId::LrcLib; }

private:
    HttpClient& http_;
};

}  // namespace openlyrics
```

- [ ] **Step 6: 构建验证**

```bash
cd ~/foo_openlyrics_mac && cmake -S . -B build && cmake --build build
```

预期：编译通过，无新增错误。TagSource/LocalFileSource 的 `fetch()` 覆写签名与基类匹配（返回 `bool`，参数 `const TrackMeta&, LyricData&`），LrcLibProvider 新旧方法共存（重载解析无歧义）。

- [ ] **Step 7: 运行现有全部测试确保无回归**

```bash
ctest --test-dir build --output-on-failure
```

预期：138/138 通过（或当前基线数量全部通过）。

- [ ] **Step 8: 提交**

```bash
git add extensions/foo_openlyrics_mac/core/model/SearchResult.h \
        extensions/foo_openlyrics_mac/core/sources/LyricSource.h \
        extensions/foo_openlyrics_mac/core/sources/TagSource.h \
        extensions/foo_openlyrics_mac/core/sources/LocalFileSource.h \
        extensions/foo_openlyrics_mac/core/sources/LrcLibProvider.h
git commit -m "扩展 LyricSource 接口与 SearchResult 结构，新增 search/fetchById/sourceId"
```

---

### Task 2: Matcher 评分器

**Files:**
- Create: `extensions/foo_openlyrics_mac/core/matching/Matcher.h`
- Create: `extensions/foo_openlyrics_mac/core/matching/Matcher.cpp`
- Create: `tests/test_matcher.cpp`

**Interfaces:**
- Produces: `Matcher::score(TrackMeta, SearchResult) → int`、`isHighConfidence(int) → bool`、`isLowConfidence(int) → bool`、`MatchWeights` 结构体

- [ ] **Step 1: 编写 Matcher 头文件**

创建 `extensions/foo_openlyrics_mac/core/matching/Matcher.h`：

```cpp
#pragma once
#include "model/TrackMeta.h"
#include "model/SearchResult.h"
#include <cstdint>
#include <string>

namespace openlyrics {

struct MatchWeights {
    float title    = 0.40f;
    float artist   = 0.25f;
    float album    = 0.15f;
    float duration = 0.20f;
};

// 文本归一化：小写 + 去标点空白 + 全角转半角 + 协作标记置换。
// 公开以便 SearchCoordinator 和测试复用。
std::string normalizeForMatch(const std::string& s);

// 分词 + Jaccard 系数。公开以便测试。
double jaccardSimilarity(const std::string& a, const std::string& b);

class Matcher {
public:
    explicit Matcher(const MatchWeights& w = {});
    int score(const TrackMeta& track, const SearchResult& candidate) const;
    bool isHighConfidence(int s) const;  // s >= 70
    bool isLowConfidence(int s) const;   // 40 <= s < 70

private:
    int scoreTitle(const std::string& a, const std::string& b) const;
    int scoreArtist(const std::string& a, const std::string& b) const;
    int scoreAlbum(const std::string& a, const std::string& b) const;
    int scoreDuration(int64_t trackMs, int candidateSec) const;

    MatchWeights w_;
    static constexpr int kHighThreshold = 70;
    static constexpr int kLowThreshold  = 40;
};

}  // namespace openlyrics
```

- [ ] **Step 2: 编写 Matcher 实现**

创建 `extensions/foo_openlyrics_mac/core/matching/Matcher.cpp`：

```cpp
#include "matching/Matcher.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>
#include <vector>

namespace openlyrics {

namespace {

// 全角字母数字转半角
char fullToHalf(unsigned char c) {
    if (c >= 0xFF01 && c <= 0xFF5E) return static_cast<char>(c - 0xFEE0);
    if (c == 0x3000) return ' ';
    return static_cast<char>(c);
}

// 协作标记置换：将 "feat. X" / "ft. X" / "featuring X" / "with X"
// 统一转为 "(feat. X)"，消除不同写法差异。
std::string normalizeCollab(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 10);
    size_t i = 0;
    while (i < s.size()) {
        // 找下一个可能是协作标记的位置（空格后）
        if (i > 0 && s[i-1] == ' ') {
            std::string tail = s.substr(i);
            // 需要精确匹配：标记+空格+内容
            auto startsWithCi = [&](const char* prefix) -> size_t {
                size_t len = 0;
                const char* p = prefix;
                size_t j = 0;
                while (*p && i + j < s.size()) {
                    if (std::tolower(static_cast<unsigned char>(s[i + j])) !=
                        std::tolower(static_cast<unsigned char>(*p)))
                        return 0;
                    ++p; ++j; ++len;
                }
                if (*p) return 0;  // prefix 未匹配完
                return len;
            };
            size_t skip = 0;
            if ((skip = startsWithCi("feat. ")) ||
                (skip = startsWithCi("ft. ")) ||
                (skip = startsWithCi("featuring "))) {
                result += "(feat. ";
                i += skip;
                // 收集艺术家名直到串尾或遇到 ,/&/(
                while (i < s.size() && s[i] != ',' && s[i] != '&' && s[i] != '(') {
                    result.push_back(s[i]);
                    ++i;
                }
                result.push_back(')');
                continue;
            }
            if ((skip = startsWithCi("with "))) {
                result += "(with ";
                i += skip;
                while (i < s.size() && s[i] != ',' && s[i] != '&' && s[i] != '(') {
                    result.push_back(s[i]);
                    ++i;
                }
                result.push_back(')');
                continue;
            }
        }
        result.push_back(s[i]);
        ++i;
    }
    return result;
}

// 按空格 / - / ( / ) / , 分词
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::string cur;
    for (unsigned char c : s) {
        if (c == ' ' || c == '-' || c == '(' || c == ')' || c == ',' || c == '/') {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(static_cast<char>(c));
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

}  // namespace

std::string normalizeForMatch(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        char h = fullToHalf(c);
        if (h == ' ') {
            // 保留一个空格作为分词分隔，但压缩连续空白
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
        } else if (std::isalnum(static_cast<unsigned char>(h))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(h))));
        }
        // 其他字符丢弃
    }
    // 去除首尾空格
    while (!out.empty() && out.back() == ' ') out.pop_back();
    size_t start = 0;
    while (start < out.size() && out[start] == ' ') ++start;
    std::string trimmed = out.substr(start);
    return normalizeCollab(trimmed);
}

double jaccardSimilarity(const std::string& a, const std::string& b) {
    auto ta = tokenize(a);
    auto tb = tokenize(b);
    if (ta.empty() && tb.empty()) return 1.0;
    std::set<std::string> setA(ta.begin(), ta.end());
    std::set<std::string> setB(tb.begin(), tb.end());
    size_t intersection = 0;
    for (const auto& w : setA) {
        if (setB.count(w)) ++intersection;
    }
    std::set<std::string> unionSet = setA;
    unionSet.insert(setB.begin(), setB.end());
    if (unionSet.empty()) return 0.0;
    return static_cast<double>(intersection) / unionSet.size();
}

namespace {

int scoreText(const std::string& a, const std::string& b,
              const double jaccardThresholds[3]) {
    if (a.empty() || b.empty()) return 0;
    std::string na = normalizeForMatch(a);
    std::string nb = normalizeForMatch(b);
    if (na.empty() || nb.empty()) return 0;
    if (na == nb) return 100;
    if (na.find(nb) != std::string::npos || nb.find(na) != std::string::npos) return 90;
    double j = jaccardSimilarity(na, nb);
    if (j >= jaccardThresholds[0]) return 80;
    if (j >= jaccardThresholds[1]) return 60;
    if (j >= jaccardThresholds[2]) return 30;
    return 0;
}

}  // namespace

Matcher::Matcher(const MatchWeights& w) : w_(w) {}

int Matcher::score(const TrackMeta& track, const SearchResult& candidate) const {
    int ts = scoreTitle(track.title, candidate.trackName);
    int as = scoreArtist(track.artist, candidate.artistName);
    int al = scoreAlbum(track.album, candidate.albumName);
    int ds = scoreDuration(track.lengthMs, candidate.durationSec);
    return static_cast<int>(std::round(ts * w_.title + as * w_.artist +
                                        al * w_.album + ds * w_.duration));
}

bool Matcher::isHighConfidence(int s) const { return s >= kHighThreshold; }
bool Matcher::isLowConfidence(int s) const { return s >= kLowThreshold && s < kHighThreshold; }

int Matcher::scoreTitle(const std::string& a, const std::string& b) const {
    static const double kTitleJaccardThresholds[3] = {0.75, 0.5, 0.25};
    return scoreText(a, b, kTitleJaccardThresholds);
}

int Matcher::scoreArtist(const std::string& a, const std::string& b) const {
    static const double kArtistJaccardThresholds[3] = {0.8, 0.6, 0.3};
    return scoreText(a, b, kArtistJaccardThresholds);
}

int Matcher::scoreAlbum(const std::string& a, const std::string& b) const {
    if (a.empty() || b.empty()) return 0;
    std::string na = normalizeForMatch(a);
    std::string nb = normalizeForMatch(b);
    if (na.empty() || nb.empty()) return 0;
    if (na.find(nb) != std::string::npos || nb.find(na) != std::string::npos) return 100;
    if (jaccardSimilarity(na, nb) >= 0.5) return 60;
    return 0;
}

int Matcher::scoreDuration(int64_t trackMs, int candidateSec) const {
    if (candidateSec <= 0 || trackMs <= 0) return 0;
    int64_t candidateMs = static_cast<int64_t>(candidateSec) * 1000;
    int64_t diff = std::abs(trackMs - candidateMs);
    if (diff <= 3000) return 100;
    if (diff <= 8000) return 70;
    if (diff <= 15000) return 40;
    return 0;
}

}  // namespace openlyrics
```

- [ ] **Step 3: 编写 Matcher 单元测试**

创建 `tests/test_matcher.cpp`：

```cpp
#include <gtest/gtest.h>
#include "matching/Matcher.h"

using namespace openlyrics;

// --- normalizeForMatch ---

TEST(NormalizeForMatch, LowerAndStripPunct) {
    EXPECT_EQ(normalizeForMatch("Hello, World!"), "hello world");
}

TEST(NormalizeForMatch, FullWidthToHalf) {
    // 全角 A (FF21) → 半角 a
    std::string fullA = "\xEF\xBC\xA1";  // UTF-8 全角 A
    EXPECT_EQ(normalizeForMatch(fullA), "a");
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
    // tokens: {a,b} vs {b,c}, intersection=1, union=3 → 1/3
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
    // tokens: {love,story,taylor} vs {love,story,taylors,version}
    // intersection: {love,story}=2, union=5 → 0.4 (<0.75, <0.5, >=0.25 → 30)

    int s = m.score(track, sr);
    EXPECT_EQ(s, 51);  // 30*0.4 + 100*0.25 + 0 + 0 = 37... wait
    // Actually: title=30 (Jaccard 0.4 -> between 0.25 and 0.5 → 30)
    // artist=100, duration=0, album=0 → 30*0.4 + 100*0.25 = 12+25 = 37
    EXPECT_EQ(s, 37);
}
```

- [ ] **Step 4: 构建并运行 Matcher 测试**

```bash
cd ~/foo_openlyrics_mac
cmake -S . -B build && cmake --build build
```

> 注意：此时 test_matcher.cpp 尚未加入 CMakeLists.txt，需在 Task 5 中添加。此步仅验证 Matcher.cpp 编译通过（加入 openlyrics_core 库后）。

先手动加入 CMakeLists.txt 以验证编译和测试。编辑 `CMakeLists.txt`：

在 `add_library(openlyrics_core STATIC` 的源文件列表末尾（`core/config/AppConfig.cpp` 之后）添加：

```
  extensions/foo_openlyrics_mac/core/matching/Matcher.cpp
```

在 `add_executable(core_tests` 的源文件列表末尾添加：

```
  tests/test_matcher.cpp
```

然后：

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

预期：新增 14 个 Matcher 测试全部通过，已有 138 个测试不受影响，共 152 个通过。

- [ ] **Step 5: 提交**

```bash
git add extensions/foo_openlyrics_mac/core/matching/Matcher.h \
        extensions/foo_openlyrics_mac/core/matching/Matcher.cpp \
        tests/test_matcher.cpp \
        CMakeLists.txt
git commit -m "新增 Matcher 评分器，支持标题/艺术家/时长/专辑四维加权评分"
```

---

### Task 3: NetEaseProvider search/fetchById

**Files:**
- Modify: `extensions/foo_openlyrics_mac/core/sources/NetEaseProvider.h`
- Modify: `extensions/foo_openlyrics_mac/core/sources/NetEaseProvider.cpp`
- Modify: `tests/test_netease_provider.cpp`

**Interfaces:**
- Consumes: `LyricSource::search/fetchById` 签名
- Produces: `NetEaseProvider::search(TrackMeta) → vector<SearchResult>`、`NetEaseProvider::fetchById(string) → LyricData`

- [ ] **Step 1: 更新 NetEaseProvider.h 声明**

在 `NetEaseProvider.h` 中，添加 `search()` 和 `fetchById()` override 声明，添加 `sourceId()`，移除 `fetch()` override（改用基类默认实现）：

```cpp
#pragma once
#include "sources/LyricSource.h"
#include "ports/HttpClient.h"
#include "ports/CryptoPort.h"

namespace openlyrics {

// 从网易云音乐按 artist+title 搜索并拉取歌词。
// 纯 C++，网络经由 HttpClient，加密经由 CryptoPort，便于 TDD。
class NetEaseProvider : public LyricSource {
public:
    NetEaseProvider(HttpClient& http, CryptoPort& crypto);

    bool search(const TrackMeta& track, std::vector<SearchResult>& out) override;
    bool fetchById(const std::string& id, LyricData& out) override;
    SourceId sourceId() const override { return SourceId::NetEase; }
    // fetch() 使用基类默认实现：search → 取第一候选 → fetchById

private:
    // weapi 加密：输入 JSON 字符串，返回 {params, encSecKey}。
    struct WeapiResult {
        std::string params;
        std::string encSecKey;
    };
    WeapiResult weapiEncrypt(const std::string& json);

    // weapi POST 并返回响应 body；status!=200 返回空串。
    std::string weapiPost(const std::string& url, const std::string& json);

    // 从搜索响应 JSON 中提取 songs 数组的多条候选。
    // 复用 extractSongs 解析 result.songs[] 的前 limit 条。
    bool extractSongs(const std::string& json, std::vector<SearchResult>& out, int limit);

    // 从单首歌曲 JSON 对象中提取 id/name/ar/al/dt 字段。
    SearchResult parseSongObject(const std::string& objJson);

    HttpClient& http_;
    CryptoPort& crypto_;
};

}  // namespace openlyrics
```

- [ ] **Step 2: 实现 NetEaseProvider::search() 和 fetchById()**

修改 `NetEaseProvider.cpp`：

(2a) 删除现有的 `fetch()` 方法实现（第 115-153 行）。

(2b) 删除 `extractFirstSongId()` 自由函数（第 47-65 行），替换为 `extractSongs()` 和 `parseSongObject()`。

(2c) 在匿名命名空间尾部（`}` 之前），添加新的辅助函数：

```cpp
// 从 JSON 数组中提取多条歌曲对象。pos 初始指向 '['，结束时指向 ']' 之后。
bool extractSongsArray(const std::string& json, size_t& pos,
                       std::vector<std::string>& out, int limit) {
    // pos 应指向 '['
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == '\r' || json[pos] == ':'))
        ++pos;
    if (pos >= json.size() || json[pos] != '[') return false;
    ++pos;  // 跳过 [
    for (int i = 0; i < limit; ++i) {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                      json[pos] == '\n' || json[pos] == '\r' || json[pos] == ','))
            ++pos;
        if (pos >= json.size() || json[pos] == ']') break;
        if (json[pos] != '{') return false;
        std::string obj;
        if (!jsonExtractObject(json, pos, obj)) break;
        out.push_back(std::move(obj));
    }
    return true;
}
```

(2d) 在 `NetEaseProvider` 类实现区域添加三个方法：

```cpp
SearchResult NetEaseProvider::parseSongObject(const std::string& objJson) {
    SearchResult sr;
    sr.source = SourceId::NetEase;

    int64_t idVal = 0;
    if (jsonGetInt(objJson, "id", idVal)) sr.id = std::to_string(idVal);
    jsonGetString(objJson, "name", sr.trackName);

    // ar 是数组：ar[0].name
    std::string arObj;
    if (jsonGetObject(objJson, "ar", arObj)) {
        // ar 可能是 "[{...}, ...]" 而非单个对象。取第一个元素的 name。
        size_t arPos = 0;
        while (arPos < arObj.size() && (arObj[arPos] == ' ' || arObj[arPos] == '\t' ||
                                         arObj[arPos] == '\n' || arObj[arPos] == '\r'))
            ++arPos;
        if (arPos < arObj.size() && arObj[arPos] == '[') {
            ++arPos;
            std::string firstAr;
            if (jsonExtractObject(arObj, arPos, firstAr)) {
                jsonGetString(firstAr, "name", sr.artistName);
            }
        }
    }

    // al 是对象：al.name
    std::string alObj;
    if (jsonGetObject(objJson, "al", alObj)) {
        jsonGetString(alObj, "name", sr.albumName);
    }

    // dt 是时长（毫秒）
    int64_t dtMs = 0;
    if (jsonGetInt(objJson, "dt", dtMs) && dtMs > 0) {
        sr.durationSec = static_cast<int>(dtMs / 1000);
    }

    return sr;
}

bool NetEaseProvider::extractSongs(const std::string& json,
                                    std::vector<SearchResult>& out, int limit) {
    size_t pos = json.find("\"songs\"");
    if (pos == std::string::npos) return false;
    pos += 7;
    std::vector<std::string> songObjects;
    if (!extractSongsArray(json, pos, songObjects, limit)) return false;
    for (const auto& obj : songObjects) {
        out.push_back(parseSongObject(obj));
    }
    return !out.empty();
}

bool NetEaseProvider::search(const TrackMeta& track, std::vector<SearchResult>& out) {
    if (track.title.empty()) return false;

    std::string searchJson =
        "{\"s\":\"" + track.artist + " " + track.title +
        "\",\"type\":1,\"offset\":0,\"limit\":5}";
    std::string searchResp = weapiPost(kSearchUrl, searchJson);
    if (searchResp.empty()) return false;

    int64_t code = 0;
    if (!jsonGetInt(searchResp, "code", code) || code != 200) return false;

    return extractSongs(searchResp, out, 5);
}

bool NetEaseProvider::fetchById(const std::string& id, LyricData& out) {
    if (id.empty()) return false;

    std::string lyricJson =
        "{\"id\":\"" + id +
        "\",\"lv\":-1,\"tv\":-1,\"cs\":-1}";
    std::string lyricResp = weapiPost(kLyricUrl, lyricJson);
    if (lyricResp.empty()) return false;

    int64_t code = 0;
    if (!jsonGetInt(lyricResp, "code", code) || code != 200) return false;

    bool noLyric = false;
    if (jsonGetBool(lyricResp, "nolyric", noLyric) && noLyric) return false;

    std::string lrcObj;
    if (!jsonGetObject(lyricResp, "lrc", lrcObj)) return false;

    std::string lrcText;
    if (!jsonGetString(lrcObj, "lyric", lrcText) || lrcText.empty()) return false;

    out = LrcParser::parse(lrcText);
    return true;
}
```

(2e) 删除文件末尾的 `fetch()` 方法实现（原第 115-153 行）。

(2f) 注意：文件头部无需添加新 `#include`，现有 include 已足够。

- [ ] **Step 3: 扩展 NetEaseProvider 测试**

在 `tests/test_netease_provider.cpp` 末尾（最后一个 TEST 之后，文件结束前）添加：

```cpp
// --- search() 测试 ---

TEST(NetEaseProvider, SearchReturnsMultipleCandidates) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, 'a');

    // 构造含 2 首歌曲的搜索响应
    http.searchResp.status = 200;
    http.searchResp.body = R"({"result":{"songs":[
        {"id":111,"name":"Song A","ar":[{"name":"Artist A"}],"al":{"name":"Album A"},"dt":200000},
        {"id":222,"name":"Song B","ar":[{"name":"Artist B"}],"al":{"name":"Album B"},"dt":250000}
    ],"songCount":2},"code":200})";

    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "Artist A";
    track.title = "Song A";

    std::vector<SearchResult> results;
    ASSERT_TRUE(provider.search(track, results));
    ASSERT_GE(results.size(), 2u);
    EXPECT_EQ(results[0].id, "111");
    EXPECT_EQ(results[0].trackName, "Song A");
    EXPECT_EQ(results[0].artistName, "Artist A");
    EXPECT_EQ(results[0].albumName, "Album A");
    EXPECT_EQ(results[0].durationSec, 200);
    EXPECT_EQ(results[0].source, SourceId::NetEase);
}

TEST(NetEaseProvider, SearchEmptyTitle) {
    FakeHttp http;
    FakeCrypto crypto;
    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.title = "";
    std::vector<SearchResult> results;
    EXPECT_FALSE(provider.search(track, results));
}

// --- fetchById() 测试 ---

TEST(NetEaseProvider, FetchByIdValid) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, 'a');

    // fetchById 只发歌词请求（1 次 POST）
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]hello\n[00:02.00]world");

    NetEaseProvider provider(http, crypto);
    LyricData out;
    ASSERT_TRUE(provider.fetchById("12345", out));
    EXPECT_EQ(out.lines.size(), 2u);
}

TEST(NetEaseProvider, FetchByIdEmptyId) {
    FakeHttp http;
    FakeCrypto crypto;
    NetEaseProvider provider(http, crypto);
    LyricData out;
    EXPECT_FALSE(provider.fetchById("", out));
}

TEST(NetEaseProvider, FetchByIdNoLyric) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, 'a');

    http.lyricResp.status = 200;
    http.lyricResp.body = R"({"code":200,"nolyric":true})";

    NetEaseProvider provider(http, crypto);
    LyricData out;
    EXPECT_FALSE(provider.fetchById("12345", out));
}

// --- fetch() 使用基类默认实现：search → fetchById ---

TEST(NetEaseProvider, FetchUsesDefaultImpl) {
    FakeHttp http;
    FakeCrypto crypto;
    crypto.aesResult = "\x01";
    crypto.rsaResult = std::string(256, 'a');

    // search 成功 + fetchById 成功
    http.searchResp.status = 200;
    http.searchResp.body = R"({"result":{"songs":[
        {"id":999,"name":"Test","ar":[{"name":"Tester"}],"al":{"name":"Test Album"},"dt":180000}
    ],"songCount":1},"code":200})";
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]a\n[00:02.00]b");

    NetEaseProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "Tester";
    track.title = "Test";
    LyricData out;

    ASSERT_TRUE(provider.fetch(track, out));
    EXPECT_EQ(out.lines.size(), 2u);
}

// sourceId() 返回 NetEase
TEST(NetEaseProvider, SourceIdIsNetEase) {
    FakeHttp http;
    FakeCrypto crypto;
    NetEaseProvider provider(http, crypto);
    EXPECT_EQ(provider.sourceId(), SourceId::NetEase);
}
```

- [ ] **Step 4: 更新现有的 FullHit 测试**

现有的 `NetEaseProvider.FullHit` 测试调用了 `provider.fetch(track, out)`，但 `fetch()` 现在使用基类默认实现（内部调用 `search()` → `fetchById()`）。需要确保测试数据同时提供 `searchResp` 和 `lyricResp`（已满足）。

无需修改 `FullHit` 测试——它已设置 `searchResp` 和 `lyricResp`，基类默认 `fetch()` 会先调用 `search()`（命中 searchResp），再调用 `fetchById()`（命中 lyricResp）。

但 `WeapiParamsPresent`、`DoubleAesEncryption`、`RsaParams` 测试也调用 `fetch()`——它们设置 `searchResp` 和 `lyricResp` 后应继续正常工作。

- [ ] **Step 5: 构建并运行所有测试**

```bash
cd ~/foo_openlyrics_mac && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

预期：所有已有 NetEase 测试 + 新增 7 个测试全部通过。

- [ ] **Step 6: 提交**

```bash
git add extensions/foo_openlyrics_mac/core/sources/NetEaseProvider.h \
        extensions/foo_openlyrics_mac/core/sources/NetEaseProvider.cpp \
        tests/test_netease_provider.cpp
git commit -m "NetEaseProvider 新增 search/fetchById，fetch 改用基类默认实现"
```

---

### Task 4: QQMusicProvider search/fetchById

**Files:**
- Modify: `extensions/foo_openlyrics_mac/core/sources/QQMusicProvider.h`
- Modify: `extensions/foo_openlyrics_mac/core/sources/QQMusicProvider.cpp`
- Modify: `tests/test_qqmusic_provider.cpp`

**Interfaces:**
- Consumes: `LyricSource::search/fetchById` 签名
- Produces: `QQMusicProvider::search(TrackMeta) → vector<SearchResult>`、`QQMusicProvider::fetchById(string) → LyricData`

- [ ] **Step 1: 更新 QQMusicProvider.h 声明**

```cpp
#pragma once
#include "sources/LyricSource.h"
#include "ports/HttpClient.h"
#include "ports/CryptoPort.h"

namespace openlyrics {

// 从 QQ 音乐按 artist+title 搜索并拉取歌词。
// 纯 C++，网络经由 HttpClient，解密经由 CryptoPort（3DES），便于 TDD。
class QQMusicProvider : public LyricSource {
public:
    QQMusicProvider(HttpClient& http, CryptoPort& crypto);

    bool search(const TrackMeta& track, std::vector<SearchResult>& out) override;
    bool fetchById(const std::string& id, LyricData& out) override;
    SourceId sourceId() const override { return SourceId::QQMusic; }
    // fetch() 使用基类默认实现：search → 取第一候选 → fetchById

private:
    // 从搜索响应 JSON 中解析 data.song.list[] 数组
    bool extractSongList(const std::string& json, std::vector<SearchResult>& out, int limit);

    HttpClient& http_;
    CryptoPort& crypto_;
};

}  // namespace openlyrics
```

- [ ] **Step 2: 实现 QQMusicProvider::search() 和 fetchById()**

修改 `QQMusicProvider.cpp`：

(2a) 删除现有的 `fetch()` 方法实现（原第 52-85 行）。

(2b) 删除 `extractFirstSongMid()` 和 `extractLyricText()` 中 fetch 专用的辅助代码。保留 `extractLyricText()`——fetchById 仍需要它。

(2c) 在匿名命名空间尾部添加：

```cpp
// 从 QQ 音乐搜索响应 JSON 中解析 data.song.list[] 数组。
bool extractSongList(const std::string& json, std::vector<SearchResult>& out, int limit) {
    // 找到 "list" 键
    size_t pos = json.find("\"list\"");
    if (pos == std::string::npos) return false;
    pos += 6;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == '\r' || json[pos] == ':'))
        ++pos;
    if (pos >= json.size() || json[pos] != '[') return false;
    ++pos;  // 跳过 [

    for (int i = 0; i < limit; ++i) {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                      json[pos] == '\n' || json[pos] == '\r' || json[pos] == ','))
            ++pos;
        if (pos >= json.size() || json[pos] == ']') break;
        if (json[pos] != '{') return false;

        std::string obj;
        if (!jsonExtractObject(json, pos, obj)) break;

        SearchResult sr;
        sr.source = SourceId::QQMusic;

        jsonGetString(obj, "songmid", sr.id);
        jsonGetString(obj, "songname", sr.trackName);

        // singer 是数组：singer[0].name
        std::string singerObj;
        if (jsonGetObject(obj, "singer", singerObj)) {
            size_t sp = 0;
            while (sp < singerObj.size() && (singerObj[sp] == ' ' || singerObj[sp] == '\t' ||
                                              singerObj[sp] == '\n' || singerObj[sp] == '\r'))
                ++sp;
            if (sp < singerObj.size() && singerObj[sp] == '[') {
                ++sp;
                std::string firstSinger;
                if (jsonExtractObject(singerObj, sp, firstSinger)) {
                    jsonGetString(firstSinger, "name", sr.artistName);
                }
            }
        }

        // albumname 可能直接是字段，也可能是 album.name
        if (!jsonGetString(obj, "albumname", sr.albumName)) {
            std::string alObj;
            if (jsonGetObject(obj, "album", alObj)) {
                jsonGetString(alObj, "name", sr.albumName);
            }
        }

        int64_t interval = 0;
        if (jsonGetInt(obj, "interval", interval) && interval > 0) {
            sr.durationSec = static_cast<int>(interval);
        }

        if (!sr.id.empty()) out.push_back(std::move(sr));
    }
    return !out.empty();
}
```

(2d) 在 `QQMusicProvider` 类实现区域添加两个方法：

```cpp
bool QQMusicProvider::search(const TrackMeta& track, std::vector<SearchResult>& out) {
    if (track.title.empty()) return false;

    std::string searchUrl = std::string(kSearchUrl) +
                            "?w=" + urlEncodeComponent(track.artist + " " + track.title) +
                            "&p=1&n=5&format=json";

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Referer", "https://y.qq.com"},
    };

    HttpResponse searchResp = http_.get(searchUrl, headers);
    if (searchResp.status != 200) return false;

    int64_t code = 0;
    if (!jsonGetInt(searchResp.body, "code", code) || code != 0) return false;

    return extractSongList(searchResp.body, out, 5);
}

bool QQMusicProvider::fetchById(const std::string& id, LyricData& out) {
    if (id.empty()) return false;

    std::string lyricUrl = std::string(kLyricUrl) +
                           "?songmid=" + urlEncodeComponent(id) +
                           "&format=json&g_tk=5381";

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Referer", "https://y.qq.com"},
    };

    HttpResponse lyricResp = http_.get(lyricUrl, headers);
    if (lyricResp.status != 200) return false;

    std::string lrcText;
    if (!extractLyricText(lyricResp.body, lrcText)) return false;

    out = LrcParser::parse(lrcText);
    return true;
}
```

(2e) 删除文件中原有的 `fetch()` 实现（第 52-85 行）。

- [ ] **Step 3: 扩展 QQMusicProvider 测试**

在 `tests/test_qqmusic_provider.cpp` 末尾添加：

```cpp
// --- search() 测试 ---

TEST(QQMusicProvider, SearchReturnsMultipleCandidates) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 200;
    http.searchResp.body = R"({"code":0,"data":{"song":{"list":[
        {"songmid":"mid1","songname":"Song A","singer":[{"name":"Artist A"}],"albumname":"Album A","interval":200},
        {"songmid":"mid2","songname":"Song B","singer":[{"name":"Artist B"}],"albumname":"Album B","interval":250}
    ],"totalnum":2}}})";

    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "Artist A";
    track.title = "Song A";

    std::vector<SearchResult> results;
    ASSERT_TRUE(provider.search(track, results));
    ASSERT_GE(results.size(), 2u);
    EXPECT_EQ(results[0].id, "mid1");
    EXPECT_EQ(results[0].trackName, "Song A");
    EXPECT_EQ(results[0].artistName, "Artist A");
    EXPECT_EQ(results[0].albumName, "Album A");
    EXPECT_EQ(results[0].durationSec, 200);
    EXPECT_EQ(results[0].source, SourceId::QQMusic);
}

TEST(QQMusicProvider, SearchEmptyTitle) {
    FakeHttp http;
    FakeCrypto crypto;
    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.title = "";
    std::vector<SearchResult> results;
    EXPECT_FALSE(provider.search(track, results));
}

// --- fetchById() 测试 ---

TEST(QQMusicProvider, FetchByIdValid) {
    FakeHttp http;
    FakeCrypto crypto;
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]hello\n[00:02.00]world");

    QQMusicProvider provider(http, crypto);
    LyricData out;
    ASSERT_TRUE(provider.fetchById("mid1", out));
    EXPECT_EQ(out.lines.size(), 2u);
}

TEST(QQMusicProvider, FetchByIdEmptyId) {
    FakeHttp http;
    FakeCrypto crypto;
    QQMusicProvider provider(http, crypto);
    LyricData out;
    EXPECT_FALSE(provider.fetchById("", out));
}

// --- fetch() 使用基类默认实现 ---

TEST(QQMusicProvider, FetchUsesDefaultImpl) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 200;
    http.searchResp.body = R"({"code":0,"data":{"song":{"list":[
        {"songmid":"mid999","songname":"Test","singer":[{"name":"Tester"}],"albumname":"Test Album","interval":180}
    ],"totalnum":1}}})";
    http.lyricResp.status = 200;
    http.lyricResp.body = makeLyricResp("[00:01.00]a\n[00:02.00]b");

    QQMusicProvider provider(http, crypto);
    TrackMeta track;
    track.artist = "Tester";
    track.title = "Test";
    LyricData out;

    ASSERT_TRUE(provider.fetch(track, out));
    EXPECT_EQ(out.lines.size(), 2u);
}

// sourceId() 返回 QQMusic
TEST(QQMusicProvider, SourceIdIsQQMusic) {
    FakeHttp http;
    FakeCrypto crypto;
    QQMusicProvider provider(http, crypto);
    EXPECT_EQ(provider.sourceId(), SourceId::QQMusic);
}
```

- [ ] **Step 4: 更新现有的 FullHit 测试**

`QQMusicProvider.FullHit` 调用 `fetch()`，现走基类默认实现（search → fetchById）。测试已设置 `searchResp` 和 `lyricResp`，无需修改。

- [ ] **Step 5: 构建并运行所有测试**

```bash
cd ~/foo_openlyrics_mac && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

预期：所有已有 QQMusic 测试 + 新增 7 个测试全部通过。

- [ ] **Step 6: 提交**

```bash
git add extensions/foo_openlyrics_mac/core/sources/QQMusicProvider.h \
        extensions/foo_openlyrics_mac/core/sources/QQMusicProvider.cpp \
        tests/test_qqmusic_provider.cpp
git commit -m "QQMusicProvider 新增 search/fetchById，fetch 改用基类默认实现"
```

---

### Task 5: LrcLibProvider 接口适配 + SearchCoordinator

**Files:**
- Modify: `extensions/foo_openlyrics_mac/core/sources/LrcLibProvider.cpp`（添加适配方法实现）
- Create: `extensions/foo_openlyrics_mac/core/pipeline/SearchCoordinator.h`
- Create: `extensions/foo_openlyrics_mac/core/pipeline/SearchCoordinator.cpp`
- Create: `tests/test_search_coordinator.cpp`

**Interfaces:**
- Consumes: `LyricSource::search/fetchById/sourceId`、`Matcher::score`、`SearchPipeline::resolve`
- Produces: `SearchCoordinator::resolve(TrackMeta, LyricData) → bool`、`SearchCoordinator::searchAll(TrackMeta) → vector<GroupedResults>`

- [ ] **Step 1: LrcLibProvider.cpp 添加适配方法实现**

在 `LrcLibProvider.cpp` 末尾（`fetchById(int, ...)` 实现之后）添加：

```cpp
// --- LyricSource 接口适配 ---

bool LrcLibProvider::search(const TrackMeta& track, std::vector<SearchResult>& out) {
    if (track.title.empty()) return false;
    std::string query = track.artist.empty() ? track.title
                                             : track.artist + " " + track.title;
    std::vector<SearchResult> raw;
    if (!search(query, raw)) return false;
    for (auto& r : raw) {
        r.source = SourceId::LrcLib;
        r.id = std::to_string(r.id);  // LrcLib 旧 SearchResult 用 int id，新接口用 string
        out.push_back(std::move(r));
    }
    return !out.empty();
}

bool LrcLibProvider::fetchById(const std::string& id, LyricData& out) {
    int intId = 0;
    try { intId = std::stoi(id); } catch (...) { return false; }
    return fetchById(intId, out);
}
```

> 注意：LrcLibProvider 现有 `SearchResult` 使用 `int id` 字段。Task 1 已将 `SearchResult::id` 改为 `std::string`，因此现有 `LrcLibProvider::search(string, ...)` 和 `LrcLibProvider::fetch()` 中 `sr.id = static_cast<int>(idVal)` 需要同步调整。编辑 `LrcLibProvider.cpp` 中 `search(const std::string& query, ...)` 方法的：
> - 第 81 行：`int64_t idVal = 0;` 保持不变
> - 第 82 行：`if (jsonGetInt(obj, "id", idVal)) sr.id = static_cast<int>(idVal);` 改为 `if (jsonGetInt(obj, "id", idVal)) sr.id = std::to_string(idVal);`
> - 第 89 行：`if (sr.id > 0)` 改为 `if (!sr.id.empty())`
> - `fetchById(int id, ...)` 方法签名保持不变，内部仍用 int

- [ ] **Step 2: 编写 SearchCoordinator.h**

创建 `extensions/foo_openlyrics_mac/core/pipeline/SearchCoordinator.h`：

```cpp
#pragma once
#include "sources/LyricSource.h"
#include "matching/Matcher.h"
#include "pipeline/SearchPipeline.h"
#include "model/LyricData.h"
#include "model/TrackMeta.h"
#include "model/SearchResult.h"
#include <vector>

namespace openlyrics {

struct GroupedResults {
    SourceId source;
    std::string sourceName;
    std::vector<SearchResult> items;  // 已含分数，降序
};

class SearchCoordinator {
public:
    SearchCoordinator(SearchPipeline& localPipeline,
                      std::vector<LyricSource*> onlineSources,
                      Matcher& matcher);

    // 自动模式：本地快速通道 → 在线候选池评分 → 取最优
    bool resolve(const TrackMeta& track, LyricData& out);

    // 手动模式：在线候选池评分 → 按源分组返回
    std::vector<GroupedResults> searchAll(const TrackMeta& track);

private:
    // 并行调用所有在线源 search()，收集候选池并评分降序
    std::vector<SearchResult> collectAndScore(const TrackMeta& track);

    SearchPipeline& localPipeline_;
    std::vector<LyricSource*> onlineSources_;
    Matcher& matcher_;
    static constexpr int kLowThreshold = 40;
};

}  // namespace openlyrics
```

- [ ] **Step 3: 编写 SearchCoordinator.cpp**

创建 `extensions/foo_openlyrics_mac/core/pipeline/SearchCoordinator.cpp`：

```cpp
#include "pipeline/SearchCoordinator.h"
#include <algorithm>
#include <map>

namespace openlyrics {

SearchCoordinator::SearchCoordinator(SearchPipeline& localPipeline,
                                     std::vector<LyricSource*> onlineSources,
                                     Matcher& matcher)
    : localPipeline_(localPipeline)
    , onlineSources_(std::move(onlineSources))
    , matcher_(matcher) {}

std::vector<SearchResult> SearchCoordinator::collectAndScore(const TrackMeta& track) {
    std::vector<SearchResult> pool;
    for (auto* source : onlineSources_) {
        if (!source) continue;
        std::vector<SearchResult> results;
        if (source->search(track, results)) {
            for (auto& r : results) {
                r.source = source->sourceId();
                r.score = matcher_.score(track, r);
                pool.push_back(std::move(r));
            }
        }
    }
    std::sort(pool.begin(), pool.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.score > b.score;
              });
    return pool;
}

bool SearchCoordinator::resolve(const TrackMeta& track, LyricData& out) {
    // 1. 本地快速通道
    if (localPipeline_.resolve(track, out)) return true;

    // 2. 在线候选池评分
    auto pool = collectAndScore(track);
    if (pool.empty()) return false;

    // 3. 最优候选
    const auto& best = pool[0];
    if (best.score < kLowThreshold) return false;

    // 4. 按 source 找到对应 provider 拉取
    for (auto* source : onlineSources_) {
        if (source && source->sourceId() == best.source) {
            return source->fetchById(best.id, out);
        }
    }
    return false;
}

std::vector<GroupedResults> SearchCoordinator::searchAll(const TrackMeta& track) {
    auto pool = collectAndScore(track);

    // 按 sourceId 分组
    std::map<SourceId, std::vector<SearchResult>> groups;
    for (auto& r : pool) {
        groups[r.source].push_back(std::move(r));
    }

    std::vector<GroupedResults> result;
    for (auto& [sid, items] : groups) {
        result.push_back({sid, sourceDisplayName(sid), std::move(items)});
    }
    return result;
}

}  // namespace openlyrics
```

- [ ] **Step 4: 编写 SearchCoordinator 单元测试**

创建 `tests/test_search_coordinator.cpp`：

```cpp
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
    SearchCoordinator coordinator(pipeline, {&fakeOnline}, matcher);

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
    SearchCoordinator coordinator(pipeline, {&fakeOnline}, matcher);

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
    SearchCoordinator coordinator(pipeline, {&fakeOnline}, matcher);

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
    SearchCoordinator coordinator(pipeline, {&netease, &qq}, matcher);

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
    SearchCoordinator coordinator(pipeline, {&bad, &good}, matcher);

    TrackMeta track;
    track.title = "晴天";
    track.artist = "周杰伦";
    track.lengthMs = 269000;

    LyricData out;
    // 应该通过 good 源成功
    EXPECT_TRUE(coordinator.resolve(track, out));
}
```

- [ ] **Step 5: 更新 CMakeLists.txt 添加新文件**

在 `CMakeLists.txt` 的 `add_library(openlyrics_core STATIC` 源文件列表中，在 `core/matching/Matcher.cpp` 之后添加：

```
  extensions/foo_openlyrics_mac/core/pipeline/SearchCoordinator.cpp
```

在 `add_executable(core_tests` 源文件列表中，在 `tests/test_matcher.cpp` 之后添加：

```
  tests/test_search_coordinator.cpp
```

- [ ] **Step 6: 构建并运行全部测试**

```bash
cd ~/foo_openlyrics_mac && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

预期：全部测试通过（138 已有 + 14 Matcher + 7 NetEase + 7 QQMusic + 5 SearchCoordinator = 171）。

- [ ] **Step 7: 提交**

```bash
git add extensions/foo_openlyrics_mac/core/sources/LrcLibProvider.cpp \
        extensions/foo_openlyrics_mac/core/pipeline/SearchCoordinator.h \
        extensions/foo_openlyrics_mac/core/pipeline/SearchCoordinator.cpp \
        tests/test_search_coordinator.cpp \
        CMakeLists.txt
git commit -m "新增 SearchCoordinator 编排层 + LrcLibProvider 接口适配"
```

---

### Task 6: LyricPanelController 接线

**Files:**
- Modify: `extensions/foo_openlyrics_mac/ui/LyricPanelController.mm`

**Interfaces:**
- Consumes: `SearchCoordinator`、`Matcher`、各 Provider 的 `search/fetchById/sourceId`
- Produces: 自动模式走 `coordinator.resolve()`，手动搜索走 `coordinator.searchAll()`

- [ ] **Step 1: 修改 handleTrackChanged 中的自动搜索逻辑**

修改 `LyricPanelController.mm` 的 `handleTrackChanged` 方法（第 551-671 行）。

在 `#include` 区域（第 26 行后）添加：

```objc
#include "matching/Matcher.h"
#include "pipeline/SearchCoordinator.h"
```

将现有的 `handleTrackChanged` 中后台 block 逻辑（第 581-670 行）替换为使用 `SearchCoordinator` 的版本。

核心改动：将现有的在线源遍历（第 620-643 行）替换为 `coordinator.resolve()` 调用。

`handleTrackChanged` 方法的新后台 block（第 581 行起）：

```objc
    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        openlyrics::TagIOAdapter tagAdapter;
        openlyrics::FileSystemAdapter fsAdapter;
        openlyrics::TagSource tagSource(tagAdapter);
        openlyrics::LocalFileSource localSource(fsAdapter);
        openlyrics::SearchPipeline localPipeline({&tagSource, &localSource});

        // 构建在线源列表（按 config.sources 顺序，仅启用的在线源）
        std::vector<openlyrics::LyricSource*> onlineSources;
        openlyrics::HttpAdapter http;
        openlyrics::CryptoAdapter crypto;
        openlyrics::LrcLibProvider lrcLib(http);
        openlyrics::NetEaseProvider netease(http, crypto);
        openlyrics::QQMusicProvider qqmusic(http, crypto);

        openlyrics::AppConfig config = _config;
        for (const auto& src : config.sources) {
            if (!src.enabled) continue;
            if (src.key == "lrclib") onlineSources.push_back(&lrcLib);
            else if (src.key == "netease") onlineSources.push_back(&netease);
            else if (src.key == "qqmusic") onlineSources.push_back(&qqmusic);
        }

        openlyrics::Matcher matcher;
        openlyrics::SearchCoordinator coordinator(localPipeline, onlineSources, matcher);

        // 失效源计数逻辑保留：Coordinator 内部对每个源独立调用 search()，
        // 单源失败不影响其他源；连续失败计数仍在此层维护。
        // 简化：Coordinator 已在 collectAndScore 中跳过失败的源（search 返回 false），
        // 失效计数由本层在 resolve 失败时递增。

        openlyrics::LyricData resolved;
        bool found = coordinator.resolve(meta, resolved);
        std::string sourceLabel = "none";

        if (found) {
            // 反查匹配源
            openlyrics::LyricData tagProbe;
            if (tagSource.fetch(meta, tagProbe)) sourceLabel = "tag";
            else {
                openlyrics::LyricData localProbe;
                if (localSource.fetch(meta, localProbe)) sourceLabel = "local";
                else sourceLabel = "online";
            }

            // 在线命中时落盘
            if (sourceLabel == "online") {
                openlyrics::LyricStore store(fsAdapter);
                store.save(meta, resolved);
            }
        }

        // 更新失效计数（简化：仅在线源且未命中时递增，命中时清零）
        if (found && sourceLabel == "online") {
            _lrclibFailures = 0;
            _neteaseFailures = 0;
            _qqmusicFailures = 0;
        } else if (!found) {
            // 所有源均失败，递增所有在线源的失效计数
            int maxFail = config.maxConsecutiveFailures;
            if (_lrclibFailures < maxFail) _lrclibFailures++;
            if (_neteaseFailures < maxFail) _neteaseFailures++;
            if (_qqmusicFailures < maxFail) _qqmusicFailures++;
        }

        FB2K_console_print("foo_openlyrics: native path=", meta.path.c_str(),
                            found ? "  lyric=matched source=" : "  lyric=not-found source=",
                            sourceLabel.c_str());

        dispatch_async(dispatch_get_main_queue(), ^{
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) return;
            if (strongSelf.trackRequestToken != requestToken) return;

            strongSelf->_currentLyricData = found ? resolved : openlyrics::LyricData{};
            strongSelf->_currentExtraOffsetMs = config.defaultOffsetMs;
            strongSelf->_currentSourceLabel = sourceLabel;
            [strongSelf.lyricView setLyricData:strongSelf->_currentLyricData];
            strongSelf.statusLabel.stringValue = found ? title
                : [NSString stringWithFormat:@"%@ · 未找到歌词", title];
            strongSelf.offsetContainer.hidden = resolved.lines.empty();
            [strongSelf updateOffsetUI];
        });
    });
```

- [ ] **Step 2: 修改 searchFieldAction 为多源搜索**

替换 `searchFieldAction:` 方法（第 313-352 行）为使用 `SearchCoordinator::searchAll()` 的版本：

```objc
- (void)searchFieldAction:(NSSearchField *)sender {
    NSString *query = [sender.stringValue stringByTrimmingCharactersInSet:
                       [NSCharacterSet whitespaceCharacterSet]];
    if (query.length == 0) return;
    [_searchPopover close];
    sender.placeholderString = @"搜索中…";

    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        // 构建 SearchCoordinator 用于搜索
        openlyrics::HttpAdapter http;
        openlyrics::CryptoAdapter crypto;
        openlyrics::LrcLibProvider lrcLib(http);
        openlyrics::NetEaseProvider netease(http, crypto);
        openlyrics::QQMusicProvider qqmusic(http, crypto);

        std::vector<openlyrics::LyricSource*> onlineSources = {&lrcLib, &netease, &qqmusic};
        openlyrics::Matcher matcher;
        openlyrics::SearchCoordinator coordinator(onlineSources, matcher);  // 手动模式无需本地管线

        // 手动搜索需要 TrackMeta，将查询字符串填入 title
        openlyrics::TrackMeta track;
        track.title = query.UTF8String;

        auto groups = coordinator.searchAll(track);

        // 转为 NSArray 供 UI 展示：每个元素是一个 section 字典
        NSMutableArray<NSDictionary *> *sections = [NSMutableArray array];
        for (const auto& g : groups) {
            NSMutableArray<NSDictionary *> *items = [NSMutableArray array];
            for (const auto& r : g.items) {
                [items addObject:@{
                    @"id": [NSString stringWithUTF8String:r.id.c_str()],
                    @"trackName": [NSString stringWithUTF8String:r.trackName.c_str()],
                    @"artistName": [NSString stringWithUTF8String:r.artistName.c_str()],
                    @"albumName": [NSString stringWithUTF8String:r.albumName.c_str()],
                    @"durationSec": @(r.durationSec),
                    @"source": @(static_cast<int>(r.source)),
                    @"sourceName": [NSString stringWithUTF8String:openlyrics::sourceDisplayName(r.source)],
                    @"score": @(r.score),
                }];
            }
            if (items.count > 0) {
                [sections addObject:@{
                    @"sourceName": [NSString stringWithUTF8String:g.sourceName.c_str()],
                    @"source": @(static_cast<int>(g.source)),
                    @"items": items,
                }];
            }
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil) return;
            strongSelf.searchField.placeholderString = @"搜索歌词…";
            strongSelf.searchSections = sections;  // 新增属性替代 _searchResults
            [strongSelf.searchTableView reloadData];
            // 计算总行数
            NSInteger totalRows = 0;
            for (NSDictionary *sec in sections) {
                totalRows += [sec[@"items"] count];
            }
            if (totalRows > 0) {
                NSViewController *vc = strongSelf.searchPopover.contentViewController;
                vc.view.frame = NSMakeRect(0, 0, 320,
                    MIN(totalRows * strongSelf.searchTableView.rowHeight + sections.count * 24 + 8, 300));
                [strongSelf.searchPopover showRelativeToRect:strongSelf.searchField.bounds
                                                      ofView:strongSelf.searchField
                                               preferredEdge:NSRectEdgeMaxY];
            }
        });
    });
}
```

> 注意：手动搜索场景需要一个"占位" SearchPipeline。最简单的方式：直接在 stack 上构造 FakeTagIO/FakeFs + TagSource/LocalFileSource + SearchPipeline。需要在 `searchFieldAction:` 中创建这些临时对象（定义在 `test_ports.cpp` 中的 Fake 类不可用，需就地定义或使用不同策略）。

更简洁的方案：为 SearchCoordinator 添加一个不需要本地管线的构造器重载，或让 searchAll 不依赖本地管线（本地管线仅在 resolve 中使用）。

在 `SearchCoordinator.h` 中添加第二构造器：

```cpp
    // 手动搜索模式：不需要本地管线
    SearchCoordinator(std::vector<LyricSource*> onlineSources, Matcher& matcher);
```

在 `SearchCoordinator.cpp` 中添加实现（将 `localPipeline_` 改为指针以支持"无本地管线"模式）：

实际上改用 `SearchPipeline*` 指针更简单。修改 SearchCoordinator：

- 将 `SearchPipeline& localPipeline_` 改为 `SearchPipeline* localPipeline_`
- 添加不带 SearchPipeline 的构造器
- `resolve()` 中判断 `localPipeline_ != nullptr` 再走快速通道

这就涉及修改 Task 5 的 SearchCoordinator。但此时 Task 5 已提交，需要在此 Task 中完成这个改动。

实际做法：在 Task 6 中修改 SearchCoordinator 支持可选的本地管线。这是此任务的一部分。

简化方案：SearchCoordinator 的 `localPipeline_` 改为指针，添加第二个构造器。

更新 `SearchCoordinator.h`：

```cpp
class SearchCoordinator {
public:
    // 带本地快速通道的构造器（自动模式）
    SearchCoordinator(SearchPipeline* localPipeline,
                      std::vector<LyricSource*> onlineSources,
                      Matcher& matcher);

    // 仅在线源的构造器（手动搜索模式）
    SearchCoordinator(std::vector<LyricSource*> onlineSources, Matcher& matcher);
    // ...其余不变

private:
    SearchPipeline* localPipeline_;  // 可为 nullptr
    // ...
};
```

更新 `SearchCoordinator.cpp`：

```cpp
SearchCoordinator::SearchCoordinator(SearchPipeline* localPipeline,
                                     std::vector<LyricSource*> onlineSources,
                                     Matcher& matcher)
    : localPipeline_(localPipeline)
    , onlineSources_(std::move(onlineSources))
    , matcher_(matcher) {}

SearchCoordinator::SearchCoordinator(std::vector<LyricSource*> onlineSources,
                                     Matcher& matcher)
    : localPipeline_(nullptr)
    , onlineSources_(std::move(onlineSources))
    , matcher_(matcher) {}

bool SearchCoordinator::resolve(const TrackMeta& track, LyricData& out) {
    if (localPipeline_ && localPipeline_->resolve(track, out)) return true;
    // ... 其余不变
}
```

更新 `LyricPanelController.mm` 中的构造调用：`SearchCoordinator coordinator(&localPipeline, onlineSources, matcher);`（传指针）。

为手动搜索：`SearchCoordinator coordinator(onlineSources, matcher);`（不传本地管线）。

- [ ] **Step 3: 更新 UI 层：添加 searchSections 属性，修改 TableView dataSource**

在 `@interface LyricPanelController ()` 的 property 区域（第 56-59 行）修改：

```objc
// 搜索
@property(nonatomic, strong) NSPopover *searchPopover;
@property(nonatomic, strong) NSTableView *searchTableView;
// 改为分组数据：每个元素是 section 字典
@property(nonatomic, copy) NSArray<NSDictionary *> *searchSections;
```

修改 `numberOfRowsInTableView:` 返回所有 section 的总行数（含 section header 行）。为简化实现，使用扁平化方案：每个 section header 作为特殊行。

修改 `searchRowDoubleClicked:` ——现在需要从 `_searchSections` 中根据 row 定位到具体的 section 和 item，并用正确的 source 调用 fetchById：

```objc
- (void)searchRowDoubleClicked:(id)sender {
    NSInteger row = _searchTableView.clickedRow;
    if (row < 0) return;
    [_searchPopover close];
    self.searchField.stringValue = @"";

    // 从 searchSections 中定位 row
    NSDictionary *item = nil;
    SourceId source = SourceId::Unknown;
    NSInteger offset = 0;
    for (NSDictionary *sec in _searchSections) {
        NSArray *items = sec[@"items"];
        NSInteger idx = row - offset;
        if (idx >= 0 && idx < (NSInteger)items.count) {
            item = items[idx];
            source = static_cast<SourceId>([sec[@"source"] intValue]);
            break;
        }
        offset += items.count;
    }
    if (!item) return;

    NSString *lyricId = item[@"id"];
    int srcInt = [item[@"source"] intValue];
    SourceId sid = static_cast<SourceId>(srcInt);

    PlaybackHub *hub = [PlaybackHub sharedHub];
    if (![hub hasTrack]) return;
    openlyrics::TrackMeta meta = [hub currentTrack];
    NSString *title = meta.title.empty() ? @"(未知曲目)"
        : [NSString stringWithUTF8String:meta.title.c_str()];
    self.statusLabel.stringValue = [NSString stringWithFormat:@"%@ · 获取歌词中…", title];

    const NSInteger requestToken = self.trackRequestToken;
    __weak __typeof__(self) weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        openlyrics::HttpAdapter http;
        openlyrics::CryptoAdapter crypto;
        openlyrics::LyricData data;
        bool ok = false;

        if (sid == SourceId::LrcLib) {
            openlyrics::LrcLibProvider provider(http);
            ok = provider.fetchById(lyricId.UTF8String, data);
        } else if (sid == SourceId::NetEase) {
            openlyrics::NetEaseProvider provider(http, crypto);
            ok = provider.fetchById(lyricId.UTF8String, data);
        } else if (sid == SourceId::QQMusic) {
            openlyrics::QQMusicProvider provider(http, crypto);
            ok = provider.fetchById(lyricId.UTF8String, data);
        }

        if (!ok) {
            dispatch_async(dispatch_get_main_queue(), ^{
                __typeof__(self) strongSelf = weakSelf;
                if (strongSelf == nil || strongSelf.trackRequestToken != requestToken) return;
                strongSelf.statusLabel.stringValue =
                    [NSString stringWithFormat:@"%@ · 获取失败", title];
            });
            return;
        }

        openlyrics::FileSystemAdapter fs;
        openlyrics::LyricStore store(fs);
        store.save(meta, data);

        dispatch_async(dispatch_get_main_queue(), ^{
            __typeof__(self) strongSelf = weakSelf;
            if (strongSelf == nil || strongSelf.trackRequestToken != requestToken) return;
            strongSelf->_currentLyricData = data;
            strongSelf->_currentExtraOffsetMs = 0;
            strongSelf->_currentSourceLabel = "search";
            [strongSelf.lyricView setLyricData:data];
            strongSelf.statusLabel.stringValue = title;
            [strongSelf updateOffsetUI];
            strongSelf.offsetContainer.hidden = data.lines.empty();
        });
    });
}
```

修改 `viewForTableColumn:` 以渲染每条结果时显示更多信息：

```objc
- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)col row:(NSInteger)row {
    // 从 searchSections 中定位 item
    NSDictionary *item = nil;
    NSInteger offset = 0;
    for (NSDictionary *sec in _searchSections) {
        NSArray *items = sec[@"items"];
        if (row - offset < (NSInteger)items.count) {
            item = items[row - offset];
            break;
        }
        offset += items.count;
    }
    if (!item) return nil;

    NSString *cellId = @"searchCell";
    NSTableCellView *cell = [tableView makeViewWithIdentifier:cellId owner:self];
    if (cell == nil) {
        cell = [[NSTableCellView alloc] initWithFrame:NSZeroRect];
        cell.identifier = cellId;
        NSTextField *tf = [NSTextField labelWithString:@""];
        tf.font = [NSFont systemFontOfSize:11];
        tf.lineBreakMode = NSLineBreakByTruncatingTail;
        tf.translatesAutoresizingMaskIntoConstraints = NO;
        [cell addSubview:tf];
        [NSLayoutConstraint activateConstraints:@[
            [tf.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:4],
            [tf.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-4],
            [tf.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        ]];
        cell.textField = tf;
    }
    NSString *sourceTag = item[@"sourceName"];
    NSNumber *score = item[@"score"];
    cell.textField.stringValue = [NSString stringWithFormat:@"[%@ %@%%] %@  —  %@",
        sourceTag, score, item[@"trackName"], item[@"artistName"]];
    return cell;
}
```

- [ ] **Step 4: 更新现有 LyricPanelController 构造调用**

`handleTrackChanged` 中的 `SearchCoordinator` 构造改为传指针：`openlyrics::SearchCoordinator coordinator(&localPipeline, onlineSources, matcher);`

- [ ] **Step 5: 搜索框 placeholder 更新**

第 81 行 `search.placeholderString` 从 `@"搜索 LrcLib…"` 改为 `@"搜索歌词…"`。

- [ ] **Step 6: 构建验证**

```bash
cd ~/foo_openlyrics_mac && cmake -S . -B build && cmake --build build
```

预期：通过编译。UI 层改动无法在命令行测试中覆盖，需在 foobar2000 中人工验证。

- [ ] **Step 7: 提交**

```bash
git add extensions/foo_openlyrics_mac/core/pipeline/SearchCoordinator.h \
        extensions/foo_openlyrics_mac/core/pipeline/SearchCoordinator.cpp \
        extensions/foo_openlyrics_mac/ui/LyricPanelController.mm
git commit -m "LyricPanelController 接入 SearchCoordinator，多源分组搜索"
```

---

### Task 7: 最终构建验证 + 全量测试

- [ ] **Step 1: 全新构建**

```bash
cd ~/foo_openlyrics_mac && rm -rf build && cmake -S . -B build && cmake --build build
```

预期：零错误零警告。

- [ ] **Step 2: 全量测试**

```bash
ctest --test-dir build --output-on-failure
```

预期：全部测试通过（约 171 个）。

- [ ] **Step 3: 提交（如有未提交变更）**

```bash
git status
# 如有遗漏文件，add + commit
```

---

### 人工验证清单

以下项目无法在命令行测试覆盖，需在 foobar2000 中确认：

1. 播放曲目 → 自动取词命中率是否提升（尤其中文歌曲）
2. 手动搜索 → 三平台候选列表按源分组展示
3. 搜索列表中每条结果显示来源标签 + 匹配分数
4. 双击搜索结果 → 正确调用对应平台的 fetchById → 歌词显示 + 落盘
5. 偏好设置中禁用某在线源 → 手动搜索不再出现该源分组
6. 断网 → 手动搜索优雅失败（不崩溃）

