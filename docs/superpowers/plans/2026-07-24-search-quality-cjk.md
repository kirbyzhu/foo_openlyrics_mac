# 网易云/QQ 搜索精准度与召回优化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让中文标题模糊匹配有效（CJK bigram）、提高候选召回（limit 5→10）、搜索前清理 query 噪声，减少"匹配错/搜不到"，同时保持英文词级匹配与现有测试不变。

**Architecture:** `Matcher` 的 `jaccardSimilarity` 分词改为 CJK 感知（中文连续段用相邻双字 token、ASCII 仍按词）；新增公开 `normalizeQuery` 清理搜索关键词；NetEase/QQ 两 Provider 提升 limit 并用 `normalizeQuery` 构造 query。全部纯逻辑，gtest 覆盖。

**Tech Stack:** C++17 核心 / GoogleTest / 既有 FakeHttp+FakeCrypto mock。

## Global Constraints

- 纯 ASCII 文本相似度行为不变：现有 `JaccardSimilarity.Identical`、`HalfOverlap` 及所有 `Matcher.*` 用例必须仍通过。
- CJK 判定：非 ASCII 字节（`c & 0x80`）即按 UTF-8 字符处理；连续非 ASCII 字符段生成相邻双字 bigram token，单字符段用单字 token。UTF-8 字符长度按首字节：`<0x80`=1、`0xC0-0xDF`=2、`0xE0-0xEF`=3、`0xF0-0xF7`=4，越界按剩余字节收尾。
- `scoreTitle`/`scoreArtist` 的 Jaccard 阈值、`MatchWeights`、`kHighThreshold`/`kLowThreshold`、`scoreDuration`/`scoreAlbum` 不改。
- 两 Provider 候选最终截断到前 10；LrcLibProvider 不改。
- query 归一化只影响搜索关键词；`Matcher` 打分仍用原始 `track.title`（`normalizeForMatch` 独立）。
- 不做版本降权、不合并翻译歌词。

---

### Task 1: Matcher CJK bigram 分词 + normalizeQuery

**Files:**
- Modify: `extensions/foo_openlyrics_mac/core/matching/Matcher.h`
- Modify: `extensions/foo_openlyrics_mac/core/matching/Matcher.cpp`
- Test: `tests/test_matcher.cpp`

**Interfaces:**
- Consumes: 既有 `normalizeForMatch`、`jaccardSimilarity`。
- Produces: 内部 `buildMatchTokens`（替换 `jaccardSimilarity` 内的 `tokenize`）；公开 `std::string normalizeQuery(const std::string& title);`（Matcher.h 声明）。

- [ ] **Step 1: 写失败测试（CJK + normalizeQuery）**

在 `tests/test_matcher.cpp` 末尾追加：

```cpp
// --- CJK bigram 相似度 ---

TEST(JaccardSimilarity, CjkIdentical) {
    EXPECT_DOUBLE_EQ(jaccardSimilarity("晴天", "晴天"), 1.0);
}

TEST(JaccardSimilarity, CjkPartialOverlap) {
    // "第一天" bigram {第一,一天}; "每一天" bigram {每一,一天}
    // 交集 {一天}=1, 并集 {第一,一天,每一}=3 -> 1/3
    EXPECT_NEAR(jaccardSimilarity("第一天", "每一天"), 1.0/3.0, 0.01);
}

TEST(JaccardSimilarity, CjkNoOverlap) {
    // "晴天" {晴天} vs "稻香" {稻香} -> 0
    EXPECT_DOUBLE_EQ(jaccardSimilarity("晴天", "稻香"), 0.0);
}

TEST(JaccardSimilarity, CjkSingleChar) {
    // 单字符段用单字 token："火" vs "火" -> 1.0
    EXPECT_DOUBLE_EQ(jaccardSimilarity("火", "火"), 1.0);
}

TEST(JaccardSimilarity, MixedCjkAscii) {
    // "陈奕迅 eason" -> CJK 段 {陈奕,奕迅} + ASCII 词 {eason}
    // 与自身 -> 1.0
    EXPECT_DOUBLE_EQ(jaccardSimilarity("陈奕迅 eason", "陈奕迅 eason"), 1.0);
}

// --- normalizeQuery ---

TEST(NormalizeQuery, StripsParens) {
    EXPECT_EQ(normalizeQuery("Love Story (Taylor's Version)"), "Love Story");
}

TEST(NormalizeQuery, StripsFullWidthParens) {
    // 中文全角括号（电影主题曲）
    EXPECT_EQ(normalizeQuery("情非得已\xEF\xBC\x88电影主题曲\xEF\xBC\x89"), "情非得已");
}

TEST(NormalizeQuery, StripsBrackets) {
    EXPECT_EQ(normalizeQuery("告白气球\xE3\x80\x90Live\xE3\x80\x91"), "告白气球");
}

TEST(NormalizeQuery, StripsFeat) {
    EXPECT_EQ(normalizeQuery("Song feat. Artist B"), "Song");
}

TEST(NormalizeQuery, PlainTitleUnchanged) {
    EXPECT_EQ(normalizeQuery("晴天"), "晴天");
}

TEST(NormalizeQuery, AllInParensFallbackToOriginal) {
    // 清理后为空则回退原串
    EXPECT_EQ(normalizeQuery("(instrumental)"), "(instrumental)");
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd ~/foo_openlyrics_mac/build && cmake --build . --target core_tests 2>&1 | tail -5`
Expected: 编译失败，`normalizeQuery` 未声明；CJK 用例断言失败（当前 jaccard 对 `第一天`/`每一天` 返回 0）。

- [ ] **Step 3: 头文件声明 normalizeQuery**

编辑 `extensions/foo_openlyrics_mac/core/matching/Matcher.h`，在 `jaccardSimilarity` 声明之后加：

```cpp
// 搜索 query 归一化：移除成对括号（含中文全角）及其内容、feat/ft 标注，折叠空白。
// 清理后为空则回退返回原串。公开以便 Provider 和测试复用。
std::string normalizeQuery(const std::string& title);
```

- [ ] **Step 4: 实现 buildMatchTokens 替换 tokenize**

编辑 `extensions/foo_openlyrics_mac/core/matching/Matcher.cpp`。在匿名命名空间内 `tokenize` 之后加 `buildMatchTokens`，并把 `jaccardSimilarity` 内的 `tokenize` 调用替换为 `buildMatchTokens`：

```cpp
// UTF-8 字符字节长度（按首字节）。
static int utf8CharLen(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;  // 非法首字节按 1 收尾
}

// CJK 感知分词：ASCII 字母数字连续段为一个词 token；
// 非 ASCII 连续段按相邻双字（bigram）生成 token，单字符段用单字。
std::vector<std::string> buildMatchTokens(const std::string& s) {
    std::vector<std::string> tokens;
    std::string asciiWord;
    std::vector<std::string> cjkRun;   // 当前非 ASCII 连续段的字符

    auto flushAscii = [&]() {
        if (!asciiWord.empty()) { tokens.push_back(asciiWord); asciiWord.clear(); }
    };
    auto flushCjk = [&]() {
        if (cjkRun.empty()) return;
        if (cjkRun.size() == 1) {
            tokens.push_back(cjkRun[0]);
        } else {
            for (size_t i = 0; i + 1 < cjkRun.size(); ++i)
                tokens.push_back(cjkRun[i] + cjkRun[i + 1]);
        }
        cjkRun.clear();
    };

    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            flushCjk();
            if (c == ' ' || c == '-' || c == '(' || c == ')' || c == ',' || c == '/') {
                flushAscii();
            } else {
                asciiWord.push_back(static_cast<char>(c));
            }
            ++i;
        } else {
            flushAscii();
            int len = utf8CharLen(c);
            if (i + len > s.size()) len = static_cast<int>(s.size() - i);
            cjkRun.push_back(s.substr(i, len));
            i += len;
        }
    }
    flushAscii();
    flushCjk();
    return tokens;
}
```

在 `jaccardSimilarity` 内把：

```cpp
    auto ta = tokenize(a);
    auto tb = tokenize(b);
```

改为：

```cpp
    auto ta = buildMatchTokens(a);
    auto tb = buildMatchTokens(b);
```

- [ ] **Step 5: 实现 normalizeQuery**

在 `Matcher.cpp` 的 `normalizeForMatch` 定义之后（匿名命名空间外、`openlyrics` 命名空间内）加：

```cpp
std::string normalizeQuery(const std::string& title) {
    // 移除成对括号及内容：ASCII () []、中文全角（）【】
    // 全角字节：（=EF BC 88, ）=EF BC 89, 【=E3 80 90, 】=E3 80 91
    std::string out;
    out.reserve(title.size());
    int depth = 0;
    size_t i = 0;
    while (i < title.size()) {
        unsigned char c = static_cast<unsigned char>(title[i]);
        // 判断是否为一个"开括号"（ASCII 或全角）
        bool isOpen = (c == '(' || c == '[');
        bool isClose = (c == ')' || c == ']');
        size_t adv = 1;
        if (c == 0xEF && i + 2 < title.size() &&
            (unsigned char)title[i+1] == 0xBC) {
            if ((unsigned char)title[i+2] == 0x88) { isOpen = true; adv = 3; }
            else if ((unsigned char)title[i+2] == 0x89) { isClose = true; adv = 3; }
        } else if (c == 0xE3 && i + 2 < title.size() &&
                   (unsigned char)title[i+1] == 0x80) {
            if ((unsigned char)title[i+2] == 0x90) { isOpen = true; adv = 3; }
            else if ((unsigned char)title[i+2] == 0x91) { isClose = true; adv = 3; }
        }
        if (isOpen) { ++depth; i += adv; continue; }
        if (isClose) { if (depth > 0) --depth; i += adv; continue; }
        if (depth == 0) out.append(title, i, adv);
        i += adv;
    }

    // 移除 feat./ft./featuring 及其后内容（大小写不敏感，词边界在空格后）
    static const char* kFeatMarkers[] = {" feat.", " feat ", " ft.", " ft ", " featuring "};
    std::string lower;
    lower.resize(out.size());
    for (size_t k = 0; k < out.size(); ++k)
        lower[k] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[k])));
    size_t cut = std::string::npos;
    for (const char* m : kFeatMarkers) {
        size_t p = lower.find(m);
        if (p != std::string::npos && p < cut) cut = p;
    }
    if (cut != std::string::npos) out = out.substr(0, cut);

    // 折叠空白 + trim
    std::string collapsed;
    collapsed.reserve(out.size());
    for (char ch : out) {
        if (ch == ' ' || ch == '\t') {
            if (!collapsed.empty() && collapsed.back() != ' ') collapsed.push_back(' ');
        } else {
            collapsed.push_back(ch);
        }
    }
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    size_t start = 0;
    while (start < collapsed.size() && collapsed[start] == ' ') ++start;
    std::string trimmed = collapsed.substr(start);

    return trimmed.empty() ? title : trimmed;
}
```

确认 `Matcher.cpp` 顶部已 `#include <cctype>`（现有，供 `std::tolower`）与 `<vector>`（现有）。

- [ ] **Step 6: 运行测试确认通过**

Run: `cd ~/foo_openlyrics_mac/build && cmake --build . --target core_tests 2>&1 | tail -3 && ./core_tests --gtest_filter='JaccardSimilarity.*:NormalizeQuery.*:NormalizeForMatch.*:Matcher.*' 2>&1 | tail -3`
Expected: 全部 PASS，含新增 CJK 与 normalizeQuery 用例，且原有 ASCII/Matcher 用例不变。

- [ ] **Step 7: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/core/matching/Matcher.h extensions/foo_openlyrics_mac/core/matching/Matcher.cpp tests/test_matcher.cpp
git commit -m "Matcher 支持中文 bigram 相似度与 query 归一化"
```

---

### Task 2: NetEaseProvider 提高召回 + query 归一化

**Files:**
- Modify: `extensions/foo_openlyrics_mac/core/sources/NetEaseProvider.cpp`
- Test: `tests/test_netease_provider.cpp`

**Interfaces:**
- Consumes: Task 1 的 `normalizeQuery`（需 `#include "matching/Matcher.h"`）。
- Produces: 无对外符号变化，行为提升。

- [ ] **Step 1: 写测试（limit=10 解析 + query 归一化）**

先查现有测试用的 FakeHttp 字段与断言风格：

Run: `grep -n "searchResp\|lastBody\|TEST(NetEase" tests/test_netease_provider.cpp`

在 `tests/test_netease_provider.cpp` 末尾追加两个用例（`searchResp` 注入含 10 个 song 对象的 JSON；query 断言查 `http.lastBody` 含清理后的 title、不含括号内容）：

```cpp
TEST(NetEaseProvider, ParsesUpToTenSongs) {
    FakeHttp http;
    FakeCrypto crypto;
    std::string songs;
    for (int i = 0; i < 10; ++i) {
        if (i) songs += ",";
        songs += "{\"id\":" + std::to_string(1000 + i) +
                 ",\"name\":\"n" + std::to_string(i) +
                 "\",\"ar\":[{\"name\":\"a\"}],\"al\":{\"name\":\"al\"},\"dt\":200000}";
    }
    http.searchResp.status = 200;
    http.searchResp.body = "{\"code\":200,\"result\":{\"songs\":[" + songs + "]}}";

    NetEaseProvider p(http, crypto);
    TrackMeta t; t.title = "n"; t.artist = "";
    std::vector<SearchResult> out;
    p.search(t, out);
    EXPECT_EQ(out.size(), 10u);
}

TEST(NetEaseProvider, QueryStripsParens) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 200;
    http.searchResp.body = "{\"code\":200,\"result\":{\"songs\":[]}}";

    NetEaseProvider p(http, crypto);
    TrackMeta t; t.title = "晴天 (Live)"; t.artist = "周杰伦";
    std::vector<SearchResult> out;
    p.search(t, out);
    // 主 query 应含清理后的 "晴天"，不含 "Live"（NetEase query 在 POST body 的 s 字段）
    EXPECT_NE(http.lastBody.find("晴天"), std::string::npos);
    EXPECT_EQ(http.lastBody.find("Live"), std::string::npos);
}
```

> `FakeHttp` 记录 `lastBody`（NetEase 用 POST），`searchResp` 用 `.status=`/`.body=` 分别赋值（现有测试风格，见 Step 1 grep）。

- [ ] **Step 2: 运行测试确认失败**

Run: `cd ~/foo_openlyrics_mac/build && cmake --build . --target core_tests 2>&1 | tail -4`
Expected: `ParsesUpToTenSongs` 失败（当前 limit=5 只解析 5 条）；`QueryStripsParens` 失败（当前 query 含 "Live"）。

- [ ] **Step 3: 提高 limit**

编辑 `NetEaseProvider.cpp` 的 `search` 内 `tryQuery`：将 `"\",\"type\":1,\"limit\":5,\"offset\":0,\"total\":true}"` 的 `limit":5` 改为 `limit":10`，并将 `extractSongs(searchResp, out, 5)` 改为 `extractSongs(searchResp, out, 10)`。

- [ ] **Step 4: query 归一化**

在 `NetEaseProvider.cpp` 顶部加 `#include "matching/Matcher.h"`。将 `search` 内：

```cpp
    std::string fullQuery = track.artist.empty() ? track.title
                                                  : track.artist + " " + track.title;
    bool ok = tryQuery(fullQuery);
```

改为：

```cpp
    std::string nq = normalizeQuery(track.title);
    std::string fullQuery = track.artist.empty() ? nq : track.artist + " " + nq;
    bool ok = tryQuery(fullQuery);
```

并将下方 title-only fallback 的 `tryQuery(track.title);` 改为 `tryQuery(nq);`。

- [ ] **Step 5: 候选截断到 10**

在 `search` 的 `return ok || !out.empty();` 之前加：

```cpp
    if (out.size() > 10) out.resize(10);
```

- [ ] **Step 6: 运行测试确认通过**

Run: `cd ~/foo_openlyrics_mac/build && cmake --build . --target core_tests 2>&1 | tail -3 && ./core_tests --gtest_filter='NetEaseProvider.*' 2>&1 | tail -3`
Expected: 全部 PASS。

- [ ] **Step 7: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/core/sources/NetEaseProvider.cpp tests/test_netease_provider.cpp
git commit -m "网易云搜索提高候选数并归一化 query"
```

---

### Task 3: QQMusicProvider 提高召回 + query 归一化

**Files:**
- Modify: `extensions/foo_openlyrics_mac/core/sources/QQMusicProvider.cpp`
- Test: `tests/test_qqmusic_provider.cpp`

**Interfaces:**
- Consumes: Task 1 的 `normalizeQuery`（需 `#include "matching/Matcher.h"`）。
- Produces: 无对外符号变化。

- [ ] **Step 1: 写测试（limit=10 解析 + query 归一化）**

先查现有测试断言风格：

Run: `grep -n "searchResp\|lastUrl\|TEST(QQMusic" tests/test_qqmusic_provider.cpp`

在 `tests/test_qqmusic_provider.cpp` 末尾追加：

```cpp
TEST(QQMusicProvider, ParsesUpToTenSongs) {
    FakeHttp http;
    FakeCrypto crypto;
    std::string list;
    for (int i = 0; i < 10; ++i) {
        if (i) list += ",";
        list += "{\"songmid\":\"m" + std::to_string(i) +
                "\",\"songname\":\"n\",\"singer\":[{\"name\":\"a\"}],"
                "\"albumname\":\"al\",\"interval\":200}";
    }
    http.searchResp.status = 200;
    http.searchResp.body = "{\"code\":0,\"data\":{\"song\":{\"list\":[" + list + "]}}}";

    QQMusicProvider p(http, crypto);
    TrackMeta t; t.title = "n"; t.artist = "";
    std::vector<SearchResult> out;
    p.search(t, out);
    EXPECT_EQ(out.size(), 10u);
}

TEST(QQMusicProvider, QueryStripsParens) {
    FakeHttp http;
    FakeCrypto crypto;
    http.searchResp.status = 200;
    http.searchResp.body = "{\"code\":0,\"data\":{\"song\":{\"list\":[]}}}";

    QQMusicProvider p(http, crypto);
    TrackMeta t; t.title = "晴天 (Live)"; t.artist = "周杰伦";
    std::vector<SearchResult> out;
    p.search(t, out);
    // QQ query 在 URL 的 w= 参数中，urlEncode 后中文/空格被转义；
    // 断言未编码的 "Live" 不出现（括号内容已被移除）
    EXPECT_EQ(http.lastUrl.find("Live"), std::string::npos);
}
```

> `searchResp` 用 `.status=`/`.body=` 分别赋值（现有测试风格）；QQ 用 GET，query 在 `lastUrl` 的 `w=` 参数。

- [ ] **Step 2: 运行测试确认失败**

Run: `cd ~/foo_openlyrics_mac/build && cmake --build . --target core_tests 2>&1 | tail -4`
Expected: `ParsesUpToTenSongs` 失败（limit=5）；`QueryStripsParens` 失败（query 含 "Live"）。

- [ ] **Step 3: 提高 limit**

编辑 `QQMusicProvider.cpp` 的 `tryQuery`：将 URL 中 `"&p=1&n=5&format=json"` 的 `n=5` 改为 `n=10`，并将 `extractSongList(searchResp.body, out, 5)` 改为 `extractSongList(searchResp.body, out, 10)`。

- [ ] **Step 4: query 归一化**

在 `QQMusicProvider.cpp` 顶部加 `#include "matching/Matcher.h"`。将 `search` 内：

```cpp
    std::string fullQuery = track.artist.empty() ? track.title
                                                  : track.artist + " " + track.title;
    bool ok = tryQuery(fullQuery);
```

改为：

```cpp
    std::string nq = normalizeQuery(track.title);
    std::string fullQuery = track.artist.empty() ? nq : track.artist + " " + nq;
    bool ok = tryQuery(fullQuery);
```

并将下方 title-only fallback 的 `tryQuery(track.title);` 改为 `tryQuery(nq);`。

- [ ] **Step 5: 候选截断到 10**

在 `search` 的 `return ok || !out.empty();` 之前加：

```cpp
    if (out.size() > 10) out.resize(10);
```

- [ ] **Step 6: 运行测试 + 全套回归**

Run: `cd ~/foo_openlyrics_mac/build && cmake --build . --target core_tests 2>&1 | tail -3 && ./core_tests 2>&1 | tail -2`
Expected: `QQMusicProvider.*` 全 PASS，且全套 `core_tests` 全绿。

- [ ] **Step 7: 构建组件并安装（供手动验证）**

Run:
```bash
cd ~/foo_openlyrics_mac/build && /opt/homebrew/bin/cmake --build . --target foo_openlyrics 2>&1 | tail -3 && cd ~/foo_openlyrics_mac && bash Scripts/install-component.sh 2>&1 | tail -1
```
Expected: 链接成功、安装完成。

- [ ] **Step 8: 提交**

```bash
cd ~/foo_openlyrics_mac
git add extensions/foo_openlyrics_mac/core/sources/QQMusicProvider.cpp tests/test_qqmusic_provider.cpp
git commit -m "QQ 音乐搜索提高候选数并归一化 query"
```

- [ ] **Step 9: 手动验证（foobar）**

重启 foobar2000，播放几首此前搜不到/匹配错的中文与英文曲目，触发在线搜索：
- 此前"搜不到"的中文歌现在能命中正确歌词。
- 标题带 `(Live)`/`(电影主题曲)`/`【】` 的曲目能搜到主歌词。
- 英文曲目匹配仍正常（无回归）。
- 右键手动搜索候选列表包含更多（至多 10）候选。

---

## 自查

- **Spec 覆盖**：中文 bigram→Task 1 Step 4；normalizeQuery→Task 1 Step 5；limit 5→10→Task 2/3 Step 3 + 截断 Step 5；query 归一化接入 Provider→Task 2/3 Step 4；英文词级保留→Task 1 buildMatchTokens 的 ASCII 分支等效原 tokenize，Step 6 回归覆盖；测试（FakeHttp 注入 10 条 + query 断言）→Task 2/3 Step 1。全部有对应任务。
- **占位扫描**：无 TBD/TODO；每个改代码步骤给出完整代码与锚点。两处 `HttpResponse` 聚合初始化字段顺序标注按现有测试 grep 结果核对后落地（Step 1 含 grep 命令），非占位。
- **类型一致**：`buildMatchTokens`(返回 `std::vector<std::string>`，与原 `tokenize` 同型，`jaccardSimilarity` 无缝替换)；`normalizeQuery`(`std::string`→`std::string`) 在 Task 1 定义、Task 2/3 调用一致；两 Provider 改动同构（limit 10、`normalizeQuery(track.title)`、截断 10）。
