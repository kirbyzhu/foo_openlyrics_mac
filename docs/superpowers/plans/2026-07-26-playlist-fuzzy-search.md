# 播放列表模糊搜索定位 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 foobar2000 主界面按 F3/Cmd+F 或右键菜单弹出悬浮搜索框，模糊搜索当前播放列表并定位到目标歌曲。

**Architecture:** core 层放纯 C++ 匹配器与拼音 cell 构建（gtest 覆盖）；platform 层用 CFStringTransform 生成拼音、遍历 playlist_manager 建快照、按 metadb_handle 定位、注册右键菜单与 initquit 快捷键监听；ui 层用 NSPanel 承载搜索框与结果列表。

**Tech Stack:** C++17、Objective-C++、Cocoa（NSPanel/NSEvent）、Core Foundation（CFStringTransform）、foobar2000 SDK（playlist_manager/contextmenu_item/initquit）、GoogleTest、CMake。

## Global Constraints

- C++ 标准 C++17（`CMAKE_CXX_STANDARD 17`）。
- 所有新增 `.mm` 文件必须同时加入 `foo_openlyrics MODULE` 源清单与其 `set_source_files_properties(... COMPILE_FLAGS "-fobjc-arc")` 列表（顶层 `CMakeLists.txt`）。
- core 层代码不得包含 Core Foundation / Cocoa 头，保持纯 C++、可进 `core_tests`。
- 拼音全部小写、去声调；匹配大小写不敏感。
- 快捷键硬编码：F3（keyCode 99）与 Cmd+F。回车仅定位不播放。
- 定位一律走 `activeplaylist_set_focus_by_handle`（抗重排），失效静默忽略。
- 构建目录：`~/foo_openlyrics_mac/build`。核心库目标 `openlyrics_core`、测试 `core_tests`、组件 `foo_openlyrics`。
- 提交信息用简体中文，结构"动词+对象"，首行 ≤50 字符，正文另起段，结尾附
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`。

---

### Task 1: core 匹配器 matchPlaylist

**Files:**
- Create: `extensions/foo_openlyrics_mac/core/search/PlaylistSearchMatcher.h`
- Create: `extensions/foo_openlyrics_mac/core/search/PlaylistSearchMatcher.cpp`
- Test: `tests/test_playlist_search_matcher.cpp`
- Modify: `CMakeLists.txt`（`openlyrics_core` 源清单加 `.cpp`；`core_tests` 加测试文件）

**Interfaces:**
- Produces:
  - `struct openlyrics::SearchCell { std::vector<std::string> alternatives; std::vector<char> initials; };`
  - `using openlyrics::SearchField = std::vector<SearchCell>;`
  - `struct openlyrics::SearchRecord { SearchField title; SearchField artist; SearchField album; };`
  - `struct openlyrics::MatchHit { std::size_t index; int score; };`
  - `int openlyrics::scoreField(const SearchField&, const std::string& query);` 未命中返回 -1，空 query 返回 0。
  - `std::vector<openlyrics::MatchHit> openlyrics::matchPlaylist(const std::vector<SearchRecord>&, const std::string& query);` query 需调用方已 trim+小写；结果按 score 降序、同分原序。

- [ ] **Step 1: 写头文件**

创建 `PlaylistSearchMatcher.h`：

```cpp
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace openlyrics {

// 一个源字符对应一个 cell。alternatives 为该字符的全拼候选（小写，非空）；
// 拉丁/数字为单字符字符串；汉字为其全部读音。initials 为各 alternative 的首字符去重。
struct SearchCell {
    std::vector<std::string> alternatives;
    std::vector<char> initials;
};

using SearchField = std::vector<SearchCell>;

struct SearchRecord {
    SearchField title;
    SearchField artist;
    SearchField album;
};

struct MatchHit {
    std::size_t index;
    int score;
};

// 未命中返回 -1；空 query 返回 0。导出以便单测。
int scoreField(const SearchField& field, const std::string& query);

// query 由调用方 trim + 转小写。空 query 命中全部（score 0，保持原序）。
std::vector<MatchHit> matchPlaylist(const std::vector<SearchRecord>& records,
                                    const std::string& query);

}  // namespace openlyrics
```

- [ ] **Step 2: 写失败测试**

创建 `tests/test_playlist_search_matcher.cpp`：

```cpp
#include <gtest/gtest.h>

#include <algorithm>

#include "search/PlaylistSearchMatcher.h"

using namespace openlyrics;

namespace {
// 从小写拉丁串建字段：每字符一个 cell。
SearchField latinField(const std::string& s) {
    SearchField f;
    for (char c : s) {
        if (c == ' ') continue;
        f.push_back(SearchCell{{std::string(1, c)}, {c}});
    }
    return f;
}
// 建一个汉字 cell（多读音）。
SearchCell hanCell(const std::vector<std::string>& readings) {
    SearchCell cell;
    cell.alternatives = readings;
    std::vector<char> ini;
    for (const auto& r : readings)
        if (!r.empty() && std::find(ini.begin(), ini.end(), r[0]) == ini.end())
            ini.push_back(r[0]);
    cell.initials = ini;
    return cell;
}
}  // namespace

TEST(ScoreField, EmptyQueryReturnsZero) {
    EXPECT_EQ(scoreField(latinField("hello"), ""), 0);
}

TEST(ScoreField, ContiguousBeatsSubsequence) {
    int contig = scoreField(latinField("hello"), "hell");
    int sub = scoreField(latinField("hello"), "hlo");
    EXPECT_GT(contig, 0);
    EXPECT_GT(sub, 0);
    EXPECT_GT(contig, sub);
}

TEST(ScoreField, NoMatchReturnsNegative) {
    EXPECT_LT(scoreField(latinField("hello"), "xyz"), 0);
}

TEST(ScoreField, PinyinFullAndInitialsPolyphonic) {
    // "银行"：银 -> yin，行 -> {hang, xing}
    SearchField field{hanCell({"yin"}), hanCell({"hang", "xing"})};
    EXPECT_GE(scoreField(field, "yinhang"), 0);  // 全拼（多音其一）
    EXPECT_GE(scoreField(field, "yinxing"), 0);  // 全拼（另一读音）
    EXPECT_GE(scoreField(field, "yh"), 0);       // 首字母
    EXPECT_GE(scoreField(field, "yx"), 0);       // 首字母（多音）
    EXPECT_LT(scoreField(field, "yz"), 0);       // 无此读音首字母
}

TEST(MatchPlaylist, TitleRanksAboveAlbum) {
    SearchRecord a;  // 命中出现在 album
    a.title = latinField("aaa");
    a.album = latinField("song");
    SearchRecord b;  // 命中出现在 title
    b.title = latinField("song");
    std::vector<SearchRecord> recs{a, b};
    auto hits = matchPlaylist(recs, "song");
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0].index, 1u);  // title 命中排前
}

TEST(MatchPlaylist, EmptyQueryReturnsAllInOrder) {
    std::vector<SearchRecord> recs{SearchRecord{latinField("a")}, SearchRecord{latinField("b")}};
    auto hits = matchPlaylist(recs, "");
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0].index, 0u);
    EXPECT_EQ(hits[1].index, 1u);
}
```

在 `CMakeLists.txt` 的 `core_tests` 源清单（`tests/test_cancel_token.cpp` 之后）加一行：

```cmake
  tests/test_playlist_search_matcher.cpp
```

- [ ] **Step 3: 运行测试确认失败**

Run: `cmake --build build --target core_tests 2>&1 | tail -20`
Expected: 编译失败，`fatal error: 'search/PlaylistSearchMatcher.h' file not found` 或链接期未定义 `scoreField`/`matchPlaylist`。

- [ ] **Step 4: 写实现**

创建 `PlaylistSearchMatcher.cpp`：

```cpp
#include "search/PlaylistSearchMatcher.h"

#include <algorithm>

namespace openlyrics {
namespace {

// 全拼模式：q 能否顺序消费 field（每 cell 跳过或消费其某个 alternative）。
bool fullModeExists(const SearchField& field, const std::string& q) {
    const std::size_t M = q.size();
    if (M == 0) return true;
    std::vector<char> reach(M + 1, 0);
    reach[0] = 1;
    for (const SearchCell& cell : field) {
        for (int j = static_cast<int>(M); j >= 0; --j) {
            if (!reach[j]) continue;
            for (const std::string& alt : cell.alternatives) {
                std::size_t L = alt.size();
                if (L > 0 && static_cast<std::size_t>(j) + L <= M &&
                    q.compare(static_cast<std::size_t>(j), L, alt) == 0) {
                    reach[j + static_cast<int>(L)] = 1;
                }
            }
        }
    }
    return reach[M] != 0;
}

// 首字母模式：q 每字符按序匹配某 cell 的一个 initial（每 cell 至多贡献一字符）。
bool initialsExists(const SearchField& field, const std::string& q) {
    if (q.empty()) return true;
    std::size_t j = 0;
    for (const SearchCell& cell : field) {
        for (char ini : cell.initials) {
            if (ini == q[j]) { ++j; break; }
        }
        if (j == q.size()) return true;
    }
    return false;
}

std::string primaryFull(const SearchField& field) {
    std::string s;
    for (const SearchCell& c : field)
        if (!c.alternatives.empty()) s += c.alternatives[0];
    return s;
}

std::string primaryInitials(const SearchField& field) {
    std::string s;
    for (const SearchCell& c : field)
        if (!c.initials.empty()) s += c.initials[0];
    return s;
}

}  // namespace

int scoreField(const SearchField& field, const std::string& query) {
    if (query.empty()) return 0;
    int score = -1;
    if (fullModeExists(field, query)) {
        int s = 600;
        if (primaryFull(field).find(query) != std::string::npos) s += 200;
        score = std::max(score, s);
    }
    if (initialsExists(field, query)) {
        int s = 300;
        if (primaryInitials(field).find(query) != std::string::npos) s += 100;
        score = std::max(score, s);
    }
    return score;
}

std::vector<MatchHit> matchPlaylist(const std::vector<SearchRecord>& records,
                                    const std::string& query) {
    std::vector<MatchHit> hits;
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (query.empty()) {
            hits.push_back({i, 0});
            continue;
        }
        const SearchRecord& r = records[i];
        int st = scoreField(r.title, query);
        int sa = scoreField(r.artist, query);
        int sb = scoreField(r.album, query);
        int best = -1;
        if (st >= 0) best = std::max(best, st + 50);
        if (sa >= 0) best = std::max(best, sa + 20);
        if (sb >= 0) best = std::max(best, sb + 0);
        if (best >= 0) hits.push_back({i, best});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const MatchHit& a, const MatchHit& b) { return a.score > b.score; });
    return hits;
}

}  // namespace openlyrics
```

在 `CMakeLists.txt` 的 `add_library(openlyrics_core STATIC ...)` 源清单末尾（`core/pipeline/SearchCoordinator.cpp` 之后）加一行：

```cmake
  extensions/foo_openlyrics_mac/core/search/PlaylistSearchMatcher.cpp
```

- [ ] **Step 5: 运行测试确认通过**

Run: `cmake --build build --target core_tests 2>&1 | tail -5 && ./build/core_tests --gtest_filter='ScoreField.*:MatchPlaylist.*'`
Expected: PASS，全部用例通过。

- [ ] **Step 6: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/core/search/PlaylistSearchMatcher.h \
        extensions/foo_openlyrics_mac/core/search/PlaylistSearchMatcher.cpp \
        tests/test_playlist_search_matcher.cpp CMakeLists.txt
git commit -m "新增播放列表搜索匹配器

多字段子序列+首字母打分，支持多音字候选读音。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: core 拼音 cell 构建与多音字表

**Files:**
- Create: `extensions/foo_openlyrics_mac/core/search/PinyinPolyphonic.h`
- Create: `extensions/foo_openlyrics_mac/core/search/PinyinPolyphonic.cpp`
- Create: `extensions/foo_openlyrics_mac/core/search/PinyinCellBuilder.h`
- Create: `extensions/foo_openlyrics_mac/core/search/PinyinCellBuilder.cpp`
- Test: `tests/test_pinyin_cell_builder.cpp`
- Modify: `CMakeLists.txt`（`openlyrics_core` 加两个 `.cpp`；`core_tests` 加测试文件）

**Interfaces:**
- Consumes: `openlyrics::SearchCell` / `SearchField`（Task 1）。
- Produces:
  - `const std::vector<std::string>* openlyrics::polyphonicReadings(char32_t cp);` 命中多音字表返回读音列表指针，否则 `nullptr`。
  - `using openlyrics::ReadingLookup = std::function<std::vector<std::string>(char32_t)>;`
  - `SearchField openlyrics::buildSearchField(const std::string& utf8, const ReadingLookup& lookup);` 拉丁/数字自建单字符 cell；汉字用 lookup 取读音建 cell（lookup 返回空则跳过该字）；其余字符跳过。

- [ ] **Step 1: 写多音字表头与实现**

创建 `PinyinPolyphonic.h`：

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace openlyrics {
// 命中常用多音字表返回其读音列表（小写、去声调），否则返回 nullptr。
const std::vector<std::string>* polyphonicReadings(char32_t cp);
}  // namespace openlyrics
```

创建 `PinyinPolyphonic.cpp`（种子表，覆盖常见多音字，后续可增量维护）：

```cpp
#include "search/PinyinPolyphonic.h"

#include <unordered_map>

namespace openlyrics {
namespace {
const std::unordered_map<char32_t, std::vector<std::string>>& table() {
    static const std::unordered_map<char32_t, std::vector<std::string>> t = {
        {U'行', {"xing", "hang"}},
        {U'长', {"chang", "zhang"}},
        {U'重', {"zhong", "chong"}},
        {U'乐', {"le", "yue"}},
        {U'中', {"zhong"}},
        {U'曲', {"qu"}},
        {U'调', {"diao", "tiao"}},
        {U'藏', {"cang", "zang"}},
        {U'都', {"dou", "du"}},
        {U'弹', {"tan", "dan"}},
        {U'和', {"he", "huo", "hai", "huan"}},
        {U'降', {"jiang", "xiang"}},
        {U'空', {"kong"}},
        {U'着', {"zhe", "zhao", "zhuo"}},
        {U'觉', {"jue", "jiao"}},
        {U'血', {"xue", "xie"}},
        {U'落', {"luo", "lao", "la"}},
        {U'背', {"bei"}},
        {U'为', {"wei"}},
        {U'干', {"gan"}},
    };
    return t;
}
}  // namespace

const std::vector<std::string>* polyphonicReadings(char32_t cp) {
    const auto& t = table();
    auto it = t.find(cp);
    return it == t.end() ? nullptr : &it->second;
}
}  // namespace openlyrics
```

- [ ] **Step 2: 写 cell 构建头**

创建 `PinyinCellBuilder.h`：

```cpp
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "search/PlaylistSearchMatcher.h"

namespace openlyrics {

// 输入一个 Hanzi 码点，返回其全拼读音列表（小写）。返回空表示无法罗马化，构建时跳过该字。
using ReadingLookup = std::function<std::vector<std::string>(char32_t)>;

// 把 UTF-8 文本构建成 SearchField：
// - ASCII 字母/数字：单字符 cell（小写）。
// - 汉字（CJK 统一表意）：用 lookup 取读音建 cell。
// - 其余（标点/空白/符号）：跳过。
SearchField buildSearchField(const std::string& utf8, const ReadingLookup& lookup);

}  // namespace openlyrics
```

- [ ] **Step 3: 写失败测试**

创建 `tests/test_pinyin_cell_builder.cpp`：

```cpp
#include <gtest/gtest.h>

#include <algorithm>

#include "search/PinyinCellBuilder.h"
#include "search/PinyinPolyphonic.h"

using namespace openlyrics;

namespace {
// 测试用假 lookup：汉字统一给一个固定读音，行/银 特殊处理。
std::vector<std::string> fakeLookup(char32_t cp) {
    if (const auto* p = polyphonicReadings(cp)) return *p;
    if (cp == U'银') return {"yin"};
    return {"x"};  // 其它汉字兜底
}
}  // namespace

TEST(PolyphonicTable, KnownEntry) {
    const auto* r = polyphonicReadings(U'行');
    ASSERT_NE(r, nullptr);
    EXPECT_NE(std::find(r->begin(), r->end(), "hang"), r->end());
    EXPECT_NE(std::find(r->begin(), r->end(), "xing"), r->end());
}

TEST(PolyphonicTable, MissEntry) {
    EXPECT_EQ(polyphonicReadings(U'银'), nullptr);
}

TEST(BuildSearchField, AsciiLowercased) {
    SearchField f = buildSearchField("Hi 9", fakeLookup);
    ASSERT_EQ(f.size(), 3u);  // H i 9，空格跳过
    EXPECT_EQ(f[0].alternatives, (std::vector<std::string>{"h"}));
    EXPECT_EQ(f[2].alternatives, (std::vector<std::string>{"9"}));
}

TEST(BuildSearchField, HanziMultiReading) {
    SearchField f = buildSearchField("银行", fakeLookup);
    ASSERT_EQ(f.size(), 2u);
    EXPECT_EQ(f[0].alternatives, (std::vector<std::string>{"yin"}));
    // 行 -> {xing, hang}
    EXPECT_EQ(f[1].alternatives.size(), 2u);
    // initials 含 x 与 h
    EXPECT_NE(std::find(f[1].initials.begin(), f[1].initials.end(), 'x'), f[1].initials.end());
    EXPECT_NE(std::find(f[1].initials.begin(), f[1].initials.end(), 'h'), f[1].initials.end());
}

TEST(BuildSearchField, PunctuationSkipped) {
    SearchField f = buildSearchField("a-b", fakeLookup);
    ASSERT_EQ(f.size(), 2u);  // '-' 跳过
}
```

在 `CMakeLists.txt` 的 `core_tests` 源清单加：

```cmake
  tests/test_pinyin_cell_builder.cpp
```

- [ ] **Step 4: 运行测试确认失败**

Run: `cmake --build build --target core_tests 2>&1 | tail -20`
Expected: 编译失败，找不到 `PinyinCellBuilder.h` / 未定义 `buildSearchField`。

- [ ] **Step 5: 写 cell 构建实现**

创建 `PinyinCellBuilder.cpp`：

```cpp
#include "search/PinyinCellBuilder.h"

#include <algorithm>
#include <cctype>

namespace openlyrics {
namespace {

// 解码 utf8[i..] 的一个码点，返回码点并把 i 推进到下一字符。非法字节按单字节处理。
char32_t decodeUtf8(const std::string& s, std::size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t n = s.size();
    if (c < 0x80) { ++i; return c; }
    int extra = 0;
    char32_t cp = 0;
    if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
    else { ++i; return c; }
    ++i;
    for (int k = 0; k < extra; ++k) {
        if (i >= n) break;
        unsigned char cc = static_cast<unsigned char>(s[i]);
        if ((cc & 0xC0) != 0x80) break;
        cp = (cp << 6) | (cc & 0x3F);
        ++i;
    }
    return cp;
}

bool isHan(char32_t cp) {
    return (cp >= 0x3400 && cp <= 0x9FFF) ||   // 扩展A + 统一表意
           (cp >= 0xF900 && cp <= 0xFAFF) ||   // 兼容表意
           (cp >= 0x20000 && cp <= 0x2FA1F);   // 扩展B+
}

std::vector<char> initialsOf(const std::vector<std::string>& alts) {
    std::vector<char> ini;
    for (const std::string& a : alts) {
        if (a.empty()) continue;
        char c = a[0];
        if (std::find(ini.begin(), ini.end(), c) == ini.end()) ini.push_back(c);
    }
    return ini;
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

SearchField buildSearchField(const std::string& utf8, const ReadingLookup& lookup) {
    SearchField field;
    std::size_t i = 0;
    while (i < utf8.size()) {
        char32_t cp = decodeUtf8(utf8, i);
        if (cp < 0x80) {
            unsigned char c = static_cast<unsigned char>(cp);
            if (std::isalnum(c)) {
                std::string a(1, static_cast<char>(std::tolower(c)));
                field.push_back(SearchCell{{a}, {a[0]}});
            }
            // 其它 ASCII（空格/标点）跳过
        } else if (isHan(cp)) {
            std::vector<std::string> readings = lookup(cp);
            std::vector<std::string> alts;
            for (std::string& r : readings) {
                std::string lr = toLower(r);
                if (!lr.empty()) alts.push_back(lr);
            }
            if (!alts.empty()) {
                field.push_back(SearchCell{alts, initialsOf(alts)});
            }
        }
        // 其它非 ASCII（全角标点等）跳过
    }
    return field;
}

}  // namespace openlyrics
```

在 `CMakeLists.txt` 的 `openlyrics_core` 源清单加两行：

```cmake
  extensions/foo_openlyrics_mac/core/search/PinyinPolyphonic.cpp
  extensions/foo_openlyrics_mac/core/search/PinyinCellBuilder.cpp
```

- [ ] **Step 6: 运行测试确认通过**

Run: `cmake --build build --target core_tests 2>&1 | tail -5 && ./build/core_tests --gtest_filter='PolyphonicTable.*:BuildSearchField.*'`
Expected: PASS。

- [ ] **Step 7: 全量回归**

Run: `./build/core_tests 2>&1 | tail -5`
Expected: 所有既有测试仍通过（无回归）。

- [ ] **Step 8: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/core/search/PinyinPolyphonic.h \
        extensions/foo_openlyrics_mac/core/search/PinyinPolyphonic.cpp \
        extensions/foo_openlyrics_mac/core/search/PinyinCellBuilder.h \
        extensions/foo_openlyrics_mac/core/search/PinyinCellBuilder.cpp \
        tests/test_pinyin_cell_builder.cpp CMakeLists.txt
git commit -m "新增拼音 cell 构建与多音字表

UTF-8 分字建 cell，汉字经 lookup 取多读音；内置常用多音字表。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: platform 快照与定位桥（PinyinBuilder + PlaylistSearchBridge）

**Files:**
- Create: `extensions/foo_openlyrics_mac/platform/PinyinBuilder.h`
- Create: `extensions/foo_openlyrics_mac/platform/PinyinBuilder.mm`
- Create: `extensions/foo_openlyrics_mac/platform/PlaylistSearchBridge.h`
- Create: `extensions/foo_openlyrics_mac/platform/PlaylistSearchBridge.mm`
- Modify: `CMakeLists.txt`（两个 `.mm` 加入 MODULE 源清单与 ARC 属性列表）

**Interfaces:**
- Consumes: `openlyrics::buildSearchField` / `ReadingLookup` / `polyphonicReadings`（Task 2）；`SearchRecord`（Task 1）。
- Produces:
  - `openlyrics::ReadingLookup openlyrics_platform::makeReadingLookup();` 组合多音字表 + CFStringTransform 兜底（带缓存）。
  - Objective-C 类 `PlaylistSnapshot`：`- (const std::vector<openlyrics::SearchRecord>&)records;`、`- (NSString *)displayTitleAt:(NSInteger)index;`、`- (BOOL)locateIndex:(NSInteger)index;`。
  - Objective-C 类 `PlaylistSearchBridge`：`+ (PlaylistSnapshot *)snapshotActivePlaylist;`。

- [ ] **Step 1: 写 PinyinBuilder 头**

创建 `PinyinBuilder.h`：

```cpp
#pragma once
#include "search/PinyinCellBuilder.h"

namespace openlyrics_platform {
// 返回一个 ReadingLookup：先查多音字表，未命中用 CFStringTransform 生成单读音（带进程内缓存）。
openlyrics::ReadingLookup makeReadingLookup();
}  // namespace openlyrics_platform
```

- [ ] **Step 2: 写 PinyinBuilder 实现**

创建 `PinyinBuilder.mm`：

```objc
#import "PinyinBuilder.h"
#import <Foundation/Foundation.h>

#include <cctype>
#include <mutex>
#include <string>
#include <unordered_map>

#include "search/PinyinPolyphonic.h"

namespace openlyrics_platform {
namespace {

// 用 CFStringTransform 把单个汉字码点转小写无声调拼音；失败返回空串。
std::string transformSingleHanzi(char32_t cp) {
    UniChar buf[2];
    CFIndex len = 0;
    if (cp <= 0xFFFF) {
        buf[len++] = static_cast<UniChar>(cp);
    } else {
        char32_t v = cp - 0x10000;
        buf[len++] = static_cast<UniChar>(0xD800 + (v >> 10));
        buf[len++] = static_cast<UniChar>(0xDC00 + (v & 0x3FF));
    }
    CFMutableStringRef s = CFStringCreateMutable(nullptr, 0);
    CFStringAppendCharacters(s, buf, len);
    CFStringTransform(s, nullptr, kCFStringTransformMandarinLatin, false);
    CFStringTransform(s, nullptr, kCFStringTransformStripCombiningMarks, false);
    std::string out;
    const char* c = CFStringGetCStringPtr(s, kCFStringEncodingUTF8);
    if (c != nullptr) {
        out = c;
    } else {
        char tmp[64];
        if (CFStringGetCString(s, tmp, sizeof(tmp), kCFStringEncodingUTF8)) out = tmp;
    }
    CFRelease(s);
    // 去空白、转小写
    std::string res;
    for (char ch : out) {
        if (ch == ' ' || ch == '\t') continue;
        res += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return res;
}

}  // namespace

openlyrics::ReadingLookup makeReadingLookup() {
    auto cache = std::make_shared<std::unordered_map<char32_t, std::string>>();
    auto mtx = std::make_shared<std::mutex>();
    return [cache, mtx](char32_t cp) -> std::vector<std::string> {
        if (const auto* p = openlyrics::polyphonicReadings(cp)) return *p;
        {
            std::lock_guard<std::mutex> lk(*mtx);
            auto it = cache->find(cp);
            if (it != cache->end()) {
                return it->second.empty() ? std::vector<std::string>{}
                                          : std::vector<std::string>{it->second};
            }
        }
        std::string r = transformSingleHanzi(cp);
        {
            std::lock_guard<std::mutex> lk(*mtx);
            (*cache)[cp] = r;
        }
        if (r.empty()) return {};
        return {r};
    };
}

}  // namespace openlyrics_platform
```

- [ ] **Step 3: 写 PlaylistSearchBridge 头**

创建 `PlaylistSearchBridge.h`：

```objc
#import <Foundation/Foundation.h>

#include <vector>
#include "search/PlaylistSearchMatcher.h"

@interface PlaylistSnapshot : NSObject
- (const std::vector<openlyrics::SearchRecord> &)records;
- (NSInteger)count;
// 供结果列表显示：返回 "标题 — 艺术家"。
- (NSString *)displayAt:(NSInteger)index;
// 在活动播放列表中聚焦+单选+滚动可见；成功返回 YES。
- (BOOL)locateIndex:(NSInteger)index;
@end

@interface PlaylistSearchBridge : NSObject
+ (PlaylistSnapshot *)snapshotActivePlaylist;
@end
```

- [ ] **Step 4: 写 PlaylistSearchBridge 实现**

创建 `PlaylistSearchBridge.mm`：

```objc
#import "PlaylistSearchBridge.h"
#import "stdafx.h"
#import "PinyinBuilder.h"

#include <string>
#include <vector>

#include "search/PinyinCellBuilder.h"

using openlyrics::SearchRecord;

@implementation PlaylistSnapshot {
    std::vector<SearchRecord> _records;
    std::vector<metadb_handle_ptr> _handles;
    NSMutableArray<NSString *> *_displays;
}

- (instancetype)initWithRecords:(std::vector<SearchRecord> &&)records
                        handles:(std::vector<metadb_handle_ptr> &&)handles
                       displays:(NSMutableArray<NSString *> *)displays {
    if ((self = [super init])) {
        _records = std::move(records);
        _handles = std::move(handles);
        _displays = displays;
    }
    return self;
}

- (const std::vector<SearchRecord> &)records { return _records; }
- (NSInteger)count { return (NSInteger)_records.size(); }

- (NSString *)displayAt:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)_displays.count) return @"";
    return _displays[index];
}

- (BOOL)locateIndex:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)_handles.size()) return NO;
    auto pm = playlist_manager::get();
    if (pm.is_empty()) return NO;
    metadb_handle_ptr h = _handles[index];
    if (h.is_empty()) return NO;
    t_size pos = pm->activeplaylist_set_focus_by_handle(h);
    if (pos == pfc_infinite) return NO;
    // 清除其它选中，仅选中 pos：affected=全部，status 仅 pos 为真。
    bit_array_true affected;
    bit_array_one status(pos);
    pm->activeplaylist_set_selection(affected, status);
    pm->activeplaylist_ensure_visible(pos);
    return YES;
}

@end

@implementation PlaylistSearchBridge

+ (PlaylistSnapshot *)snapshotActivePlaylist {
    std::vector<SearchRecord> records;
    std::vector<metadb_handle_ptr> handles;
    NSMutableArray<NSString *> *displays = [NSMutableArray array];

    auto pm = playlist_manager::get();
    if (pm.is_empty()) {
        return [[PlaylistSnapshot alloc] initWithRecords:std::move(records)
                                                 handles:std::move(handles)
                                                displays:displays];
    }
    t_size count = pm->activeplaylist_get_item_count();
    auto lookup = openlyrics_platform::makeReadingLookup();

    for (t_size i = 0; i < count; ++i) {
        metadb_handle_ptr h;
        if (!pm->activeplaylist_get_item_handle(h, i) || h.is_empty()) continue;

        std::string title, artist, album;
        metadb_info_container::ptr infoRef;
        if (h->get_info_ref(infoRef)) {
            const file_info &info = infoRef->info();
            const char *t = info.meta_get_title(nullptr);
            if (t) title = t;
            if (info.meta_get_count_by_name("artist") > 0) {
                const char *a = info.meta_get("artist", 0);
                if (a) artist = a;
            }
            if (info.meta_get_count_by_name("album") > 0) {
                const char *a = info.meta_get("album", 0);
                if (a) album = a;
            }
        }
        if (title.empty()) {
            // 用文件名兜底标题
            std::string path = h->get_path();
            size_t slash = path.find_last_of('/');
            title = (slash == std::string::npos) ? path : path.substr(slash + 1);
        }

        SearchRecord rec;
        rec.title = openlyrics::buildSearchField(title, lookup);
        rec.artist = openlyrics::buildSearchField(artist, lookup);
        rec.album = openlyrics::buildSearchField(album, lookup);
        records.push_back(std::move(rec));
        handles.push_back(h);

        NSString *tt = [NSString stringWithUTF8String:title.c_str()] ?: @"";
        NSString *aa = [NSString stringWithUTF8String:artist.c_str()] ?: @"";
        NSString *disp = aa.length ? [NSString stringWithFormat:@"%@ — %@", tt, aa] : tt;
        [displays addObject:disp];
    }

    return [[PlaylistSnapshot alloc] initWithRecords:std::move(records)
                                             handles:std::move(handles)
                                            displays:displays];
}

@end
```

在 `CMakeLists.txt` 的 `add_library(foo_openlyrics MODULE ...)` 源清单加：

```cmake
    extensions/foo_openlyrics_mac/platform/PinyinBuilder.mm
    extensions/foo_openlyrics_mac/platform/PlaylistSearchBridge.mm
```

并在其后 `set_source_files_properties(... PROPERTIES COMPILE_FLAGS "-fobjc-arc")` 的文件列表加同样两行。

- [ ] **Step 5: 编译组件确认通过**

Run: `cmake --build build --target foo_openlyrics 2>&1 | tail -15`
Expected: 编译链接成功，产出 `build/foo_openlyrics.component`（无 metadb/playlist API 用法错误）。

说明：本任务无纯逻辑单测（依赖 CFStringTransform 与 SDK 运行时），功能在 Task 5/6 装入 foobar 后随搜索框一并手动验证。

- [ ] **Step 6: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/platform/PinyinBuilder.h \
        extensions/foo_openlyrics_mac/platform/PinyinBuilder.mm \
        extensions/foo_openlyrics_mac/platform/PlaylistSearchBridge.h \
        extensions/foo_openlyrics_mac/platform/PlaylistSearchBridge.mm CMakeLists.txt
git commit -m "新增播放列表快照与定位桥

CFStringTransform 生成拼音兜底，遍历活动列表建搜索记录，
按 metadb_handle 聚焦选中定位。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: ui 悬浮搜索框 PlaylistSearchController

**Files:**
- Create: `extensions/foo_openlyrics_mac/ui/PlaylistSearchController.h`
- Create: `extensions/foo_openlyrics_mac/ui/PlaylistSearchController.mm`
- Modify: `CMakeLists.txt`（`.mm` 加入 MODULE 源清单与 ARC 属性列表）

**Interfaces:**
- Consumes: `PlaylistSearchBridge` / `PlaylistSnapshot`（Task 3）；`openlyrics::matchPlaylist`（Task 1）。
- Produces: `+ (instancetype)shared;`、`- (void)showOrFocus;`（幂等：未显示则建快照并显示，已显示则聚焦搜索框）。

- [ ] **Step 1: 写头文件**

创建 `PlaylistSearchController.h`：

```objc
#import <Cocoa/Cocoa.h>

@interface PlaylistSearchController : NSObject
+ (instancetype)shared;
// 弹出/聚焦搜索框。必须在主线程调用。
- (void)showOrFocus;
@end
```

- [ ] **Step 2: 写实现**

创建 `PlaylistSearchController.mm`：

```objc
#import "PlaylistSearchController.h"
#import "PlaylistSearchBridge.h"

#include <string>
#include "search/PlaylistSearchMatcher.h"

@interface PlaylistSearchController () <NSTextFieldDelegate, NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSPanel *panel;
@property(nonatomic, strong) NSTextField *searchField;
@property(nonatomic, strong) NSTableView *resultTable;
@property(nonatomic, strong) PlaylistSnapshot *snapshot;
@end

@implementation PlaylistSearchController {
    std::vector<openlyrics::MatchHit> _hits;
}

+ (instancetype)shared {
    static PlaylistSearchController *inst = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ inst = [[PlaylistSearchController alloc] init]; });
    return inst;
}

- (void)buildPanel {
    NSRect frame = NSMakeRect(0, 0, 480, 320);
    self.panel = [[NSPanel alloc] initWithContentRect:frame
                                            styleMask:(NSWindowStyleMaskTitled |
                                                       NSWindowStyleMaskClosable |
                                                       NSWindowStyleMaskFullSizeContentView)
                                              backing:NSBackingStoreBuffered
                                                defer:YES];
    self.panel.titleVisibility = NSWindowTitleHidden;
    self.panel.titlebarAppearsTransparent = YES;
    self.panel.movableByWindowBackground = YES;
    self.panel.delegate = self;
    self.panel.hidesOnDeactivate = NO;

    NSView *content = self.panel.contentView;

    self.searchField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    self.searchField.translatesAutoresizingMaskIntoConstraints = NO;
    self.searchField.placeholderString = @"搜索当前播放列表（标题/艺术家/专辑，支持拼音）";
    self.searchField.font = [NSFont systemFontOfSize:15];
    self.searchField.delegate = self;
    self.searchField.bezelStyle = NSTextFieldRoundedBezel;
    [content addSubview:self.searchField];

    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = YES;
    self.resultTable = [[NSTableView alloc] initWithFrame:NSZeroRect];
    self.resultTable.headerView = nil;
    self.resultTable.rowHeight = 22;
    self.resultTable.dataSource = self;
    self.resultTable.delegate = self;
    self.resultTable.doubleAction = @selector(locateSelected);
    self.resultTable.target = self;
    NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:@"disp"];
    col.width = 440;
    [self.resultTable addTableColumn:col];
    scroll.documentView = self.resultTable;
    [content addSubview:scroll];

    [NSLayoutConstraint activateConstraints:@[
        [self.searchField.topAnchor constraintEqualToAnchor:content.topAnchor constant:28],
        [self.searchField.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [self.searchField.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
        [scroll.topAnchor constraintEqualToAnchor:self.searchField.bottomAnchor constant:10],
        [scroll.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
        [scroll.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-16],
    ]];
}

- (void)showOrFocus {
    if (self.panel == nil) [self buildPanel];
    if (!self.panel.isVisible) {
        self.snapshot = [PlaylistSearchBridge snapshotActivePlaylist];
        self.searchField.stringValue = @"";
        [self refilter];
        NSWindow *host = NSApp.keyWindow ?: NSApp.mainWindow;
        if (host) {
            NSRect hf = host.frame;
            NSRect pf = self.panel.frame;
            NSPoint origin = NSMakePoint(NSMidX(hf) - pf.size.width / 2,
                                         NSMidY(hf) - pf.size.height / 2);
            [self.panel setFrameOrigin:origin];
        } else {
            [self.panel center];
        }
        [self.panel makeKeyAndOrderFront:nil];
    }
    [self.panel makeFirstResponder:self.searchField];
}

- (void)refilter {
    std::string q = self.searchField.stringValue.lowercaseString.UTF8String ?: "";
    if (self.snapshot) {
        _hits = openlyrics::matchPlaylist([self.snapshot records], q);
    } else {
        _hits.clear();
    }
    [self.resultTable reloadData];
    if (!_hits.empty()) {
        [self.resultTable selectRowIndexes:[NSIndexSet indexSetWithIndex:0]
                      byExtendingSelection:NO];
    }
}

- (void)controlTextDidChange:(NSNotification *)obj { [self refilter]; }

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    return (NSInteger)_hits.size();
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)col row:(NSInteger)row {
    NSTableCellView *cell = [tableView makeViewWithIdentifier:@"c" owner:self];
    if (cell == nil) {
        cell = [[NSTableCellView alloc] initWithFrame:NSZeroRect];
        cell.identifier = @"c";
        NSTextField *tf = [NSTextField labelWithString:@""];
        tf.translatesAutoresizingMaskIntoConstraints = NO;
        [cell addSubview:tf];
        [NSLayoutConstraint activateConstraints:@[
            [tf.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:4],
            [tf.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        ]];
        cell.textField = tf;
    }
    if (row >= 0 && row < (NSInteger)_hits.size()) {
        cell.textField.stringValue = [self.snapshot displayAt:(NSInteger)_hits[row].index];
    }
    return cell;
}

- (void)locateSelected {
    NSInteger row = self.resultTable.selectedRow;
    if (row < 0 || row >= (NSInteger)_hits.size()) return;
    [self.snapshot locateIndex:(NSInteger)_hits[row].index];
    [self closePanel];
}

- (void)closePanel {
    [self.panel orderOut:nil];
    self.snapshot = nil;
    _hits.clear();
}

// 搜索框内截获方向键/回车/Esc
- (BOOL)control:(NSControl *)control textView:(NSTextView *)textView doCommandBySelector:(SEL)sel {
    if (sel == @selector(moveDown:)) {
        NSInteger next = MIN(self.resultTable.selectedRow + 1, (NSInteger)_hits.size() - 1);
        if (next >= 0) [self.resultTable selectRowIndexes:[NSIndexSet indexSetWithIndex:next] byExtendingSelection:NO];
        [self.resultTable scrollRowToVisible:next];
        return YES;
    }
    if (sel == @selector(moveUp:)) {
        NSInteger prev = MAX(self.resultTable.selectedRow - 1, 0);
        [self.resultTable selectRowIndexes:[NSIndexSet indexSetWithIndex:prev] byExtendingSelection:NO];
        [self.resultTable scrollRowToVisible:prev];
        return YES;
    }
    if (sel == @selector(insertNewline:)) { [self locateSelected]; return YES; }
    if (sel == @selector(cancelOperation:)) { [self closePanel]; return YES; }
    return NO;
}

- (void)windowDidResignKey:(NSNotification *)notification { [self closePanel]; }

@end
```

在 `CMakeLists.txt` 的 MODULE 源清单与 ARC 属性列表各加：

```cmake
    extensions/foo_openlyrics_mac/ui/PlaylistSearchController.mm
```

- [ ] **Step 3: 编译组件确认通过**

Run: `cmake --build build --target foo_openlyrics 2>&1 | tail -15`
Expected: 编译链接成功。此时尚无触发入口，功能在 Task 5 装入后验证。

- [ ] **Step 4: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/ui/PlaylistSearchController.h \
        extensions/foo_openlyrics_mac/ui/PlaylistSearchController.mm CMakeLists.txt
git commit -m "新增悬浮搜索框控制器

NSPanel 承载搜索框与结果列表，即时过滤、方向键选择、
回车定位、Esc/失焦关闭。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: 快捷键监听（initquit + NSEvent）并注册

**Files:**
- Create: `extensions/foo_openlyrics_mac/platform/PlaylistSearchHotkey.mm`
- Modify: `CMakeLists.txt`（`.mm` 加入 MODULE 源清单与 ARC 属性列表）

**Interfaces:**
- Consumes: `PlaylistSearchController`（Task 4）。
- Produces: 一个 `initquit` 服务，`on_init` 装 NSEvent 本地监听，F3/Cmd+F → `[[PlaylistSearchController shared] showOrFocus]`。

- [ ] **Step 1: 写实现**

创建 `PlaylistSearchHotkey.mm`：

```objc
#import "stdafx.h"
#import <Cocoa/Cocoa.h>
#import "PlaylistSearchController.h"

namespace {

static id g_monitor = nil;

class PlaylistSearchHotkey : public initquit {
public:
    void on_init() override {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (g_monitor != nil) return;
            g_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                              handler:^NSEvent *(NSEvent *event) {
                BOOL isF3 = (event.keyCode == 99);
                BOOL isCmdF = (event.modifierFlags & NSEventModifierFlagCommand) &&
                              [event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"f"];
                if (isF3 || isCmdF) {
                    [[PlaylistSearchController shared] showOrFocus];
                    return nil;  // 吞掉事件，覆盖宿主处理
                }
                return event;
            }];
        });
    }

    void on_quit() override {
        // on_quit 在主线程；直接移除
        if (g_monitor != nil) {
            [NSEvent removeMonitor:g_monitor];
            g_monitor = nil;
        }
    }
};

initquit_factory_t<PlaylistSearchHotkey> g_playlist_search_hotkey_factory;

}  // namespace
```

在 `CMakeLists.txt` 的 MODULE 源清单与 ARC 属性列表各加：

```cmake
    extensions/foo_openlyrics_mac/platform/PlaylistSearchHotkey.mm
```

- [ ] **Step 2: 编译组件**

Run: `cmake --build build --target foo_openlyrics 2>&1 | tail -10`
Expected: 编译链接成功，产出 `build/foo_openlyrics.component`。

- [ ] **Step 3: 安装并真机验证**

```bash
osascript -e 'tell application "foobar2000" to quit' 2>/dev/null; sleep 1; pkill -x foobar2000 2>/dev/null; sleep 1
DST=~/Library/foobar2000-v2/user-components/foo_openlyrics/foo_openlyrics.component
rm -rf "$DST" && cp -R ~/foo_openlyrics_mac/build/foo_openlyrics.component "$DST"
open -a foobar2000
```

手动验证（在 foobar 主界面，当前播放列表非空）：
- 按 F3 → 悬浮搜索框弹出并聚焦。
- 输入标题片段/拼音首字母 → 结果实时过滤。
- ↑↓ 选择，回车 → 播放列表滚动并选中目标歌曲，搜索框关闭，且**未开始播放**。
- 再按 Cmd+F → 搜索框弹出。
- Esc 或点击别处 → 搜索框关闭。

Expected: 以上均符合。若组件启动即退，检查 `~/foo_openlyrics_mac/build/foo_openlyrics.component` 前台运行输出 `/Applications/foobar2000.app/Contents/MacOS/foobar2000` 的控制台日志排错。

- [ ] **Step 4: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/platform/PlaylistSearchHotkey.mm CMakeLists.txt
git commit -m "新增搜索框快捷键监听

initquit 装 NSEvent 本地监听，F3/Cmd+F 弹出搜索框。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: 歌单右键菜单入口并注册

**Files:**
- Create: `extensions/foo_openlyrics_mac/platform/PlaylistSearchContextMenu.mm`
- Modify: `CMakeLists.txt`（`.mm` 加入 MODULE 源清单与 ARC 属性列表）

**Interfaces:**
- Consumes: `PlaylistSearchController`（Task 4）。
- Produces: 一个 `contextmenu_item_simple` 服务，显示 `搜索定位歌曲  (F3 / ⌘F)`，执行时打开搜索框。

- [ ] **Step 1: 写实现**

创建 `PlaylistSearchContextMenu.mm`：

```objc
#import "stdafx.h"
#import <Cocoa/Cocoa.h>
#import "PlaylistSearchController.h"

namespace {

// {C8B4A7E1-3F2D-4A6B-9C1E-7D5F0A2B8E44}
static const GUID g_search_locate_cmd_guid =
    { 0xc8b4a7e1, 0x3f2d, 0x4a6b, { 0x9c, 0x1e, 0x7d, 0x5f, 0x0a, 0x2b, 0x8e, 0x44 } };

class PlaylistSearchContextMenu : public contextmenu_item_simple {
public:
    unsigned get_num_items() override { return 1; }

    void get_item_name(unsigned, pfc::string_base &out) override {
        out = "搜索定位歌曲  (F3 / ⌘F)";
    }

    void context_command(unsigned, metadb_handle_list_cref, const GUID &) override {
        dispatch_async(dispatch_get_main_queue(), ^{
            [[PlaylistSearchController shared] showOrFocus];
        });
    }

    GUID get_item_guid(unsigned) override { return g_search_locate_cmd_guid; }

    bool get_item_description(unsigned, pfc::string_base &out) override {
        out = "在当前播放列表中模糊搜索并定位歌曲";
        return true;
    }
};

FB2K_SERVICE_FACTORY(PlaylistSearchContextMenu);

}  // namespace
```

在 `CMakeLists.txt` 的 MODULE 源清单与 ARC 属性列表各加：

```cmake
    extensions/foo_openlyrics_mac/platform/PlaylistSearchContextMenu.mm
```

- [ ] **Step 2: 编译组件**

Run: `cmake --build build --target foo_openlyrics 2>&1 | tail -10`
Expected: 编译链接成功。

- [ ] **Step 3: 安装并真机验证**

```bash
osascript -e 'tell application "foobar2000" to quit' 2>/dev/null; sleep 1; pkill -x foobar2000 2>/dev/null; sleep 1
DST=~/Library/foobar2000-v2/user-components/foo_openlyrics/foo_openlyrics.component
rm -rf "$DST" && cp -R ~/foo_openlyrics_mac/build/foo_openlyrics.component "$DST"
open -a foobar2000
```

手动验证：右键点击播放列表中的曲目 → 菜单出现 `搜索定位歌曲  (F3 / ⌘F)` → 点击 → 悬浮搜索框弹出，功能同 Task 5。

Expected: 菜单项存在且键位提示可见，点击可打开搜索框。

- [ ] **Step 4: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/platform/PlaylistSearchContextMenu.mm CMakeLists.txt
git commit -m "新增歌单右键搜索定位入口

contextmenu_item 显示带键位提示，点击打开搜索框。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## 自检记录

- **spec 覆盖**：悬浮框（Task 4）、F3/Cmd+F 监听（Task 5）、右键入口带键位提示（Task 6）、当前播放列表快照+handle 定位（Task 3）、多字段子序列+首字母匹配（Task 1）、拼音+多音字（Task 2）、core 纯逻辑 gtest（Task 1/2）、回车定位不播放（Task 4 locateSelected 不触发 playback）。均有对应任务。
- **类型一致性**：`SearchCell/SearchField/SearchRecord/MatchHit`（Task 1）→ Task 2 `buildSearchField` 产出、Task 3 填充、Task 4 消费；`ReadingLookup`（Task 2）↔ `makeReadingLookup`（Task 3）；`PlaylistSnapshot`/`PlaylistSearchBridge`（Task 3）↔ Task 4 调用；`showOrFocus`（Task 4）↔ Task 5/6 调用。签名一致。
- **占位符**：无 TBD/TODO；代码块均为可编译内容。SDK 用法已核实：`bit_array_true`/`bit_array_one` 存在，`activeplaylist_set_focus_by_handle` 返回索引（`pfc_infinite` 为未找到），`contextmenu_item_simple` 的必重写纯虚（get_num_items/get_item_name/context_command/get_item_guid/get_item_description）均已覆盖。
