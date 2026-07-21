# 计划一 可移植核心与项目骨架 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 搭起项目骨架与命令行测试脚手架，实现 Layer A 可移植核心中不依赖 SDK 的基石模块（歌词模型、LRC 解析/序列化、同步引擎、平台端口抽象），全部以真实测试覆盖。

**Architecture:** 纯 C++17 静态库 `openlyrics_core`，零 foobar2000 与 AppKit 依赖，用 CMake + GoogleTest 在命令行构建与测试。后续计划的 SDK 胶水层（Layer B）与 AppKit 面板（Layer C）通过本层定义的端口接口反向依赖本层。

**Tech Stack:** C++17、CMake ≥ 3.20、GoogleTest v1.15.2（FetchContent 拉取）。

## Global Constraints

- 语言标准 C++17，`CMAKE_CXX_STANDARD_REQUIRED ON`。
- 命名空间统一 `openlyrics`。
- Layer A 核心禁止 `#include` 任何 foobar2000 SDK 头、任何 AppKit/Cocoa 头、任何 Objective-C。仅用标准库。
- 源码根 `extensions/foo_openlyrics_mac/core/`，测试根 `tests/`。
- 时间单位统一毫秒，类型 `int64_t`。
- 每个任务末尾提交一次，提交信息用简体中文，结构"动词+对象"。

---

### Task 1: 项目骨架与测试脚手架

**Files:**
- Create: `CMakeLists.txt`
- Create: `tests/test_smoke.cpp`

**Interfaces:**
- Consumes: 无
- Produces: 可用命令 `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure` 构建并运行测试。GoogleTest 目标 `GTest::gtest_main` 可用。

- [ ] **Step 1: 写冒烟测试**

`tests/test_smoke.cpp`
```cpp
#include <gtest/gtest.h>

TEST(Smoke, HarnessWorks) {
    EXPECT_EQ(1 + 1, 2);
}
```

- [ ] **Step 2: 写 CMakeLists**

`CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.20)
project(foo_openlyrics_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
FetchContent_MakeAvailable(googletest)

add_library(openlyrics_core INTERFACE)
target_include_directories(openlyrics_core INTERFACE extensions/foo_openlyrics_mac/core)

enable_testing()
add_executable(core_tests
  tests/test_smoke.cpp
)
target_link_libraries(core_tests PRIVATE openlyrics_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(core_tests)
```

- [ ] **Step 3: 构建并运行，确认通过**

Run:
```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: `core_tests` 编译成功，`Smoke.HarnessWorks` PASS，ctest 报 100% passed。

- [ ] **Step 4: 提交**

```bash
echo "build/" >> .gitignore
git add CMakeLists.txt tests/test_smoke.cpp .gitignore
git commit -m "搭建 CMake 与 GoogleTest 测试脚手架"
```

---

### Task 2: LyricData 模型与 LrcParser 基础解析

**Files:**
- Create: `extensions/foo_openlyrics_mac/core/model/LyricData.h`
- Create: `extensions/foo_openlyrics_mac/core/parser/LrcParser.h`
- Create: `extensions/foo_openlyrics_mac/core/parser/LrcParser.cpp`
- Create: `tests/test_lrc_parser.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: 无
- Produces:
  - `struct openlyrics::Syllable { int64_t startMs; std::string text; };`
  - `struct openlyrics::LyricLine { int64_t timeMs; std::string text; std::vector<Syllable> syllables; };`（无时标行 `timeMs == -1`）
  - `struct openlyrics::LyricData { std::vector<LyricLine> lines; std::vector<std::pair<std::string,std::string>> tags; int64_t offsetMs = 0; bool synced = false; };`
  - `static LyricData openlyrics::LrcParser::parse(const std::string& text);`

- [ ] **Step 1: 写模型头文件**

`extensions/foo_openlyrics_mac/core/model/LyricData.h`
```cpp
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace openlyrics {

struct Syllable {          // 预留 word-level，首版不填充
    int64_t startMs = 0;
    std::string text;
};

struct LyricLine {
    int64_t timeMs = -1;               // 行起始时标，毫秒；无时标行为 -1
    std::string text;
    std::vector<Syllable> syllables;   // 预留，首版为空
};

struct LyricData {
    std::vector<LyricLine> lines;                              // synced 行按 timeMs 升序
    std::vector<std::pair<std::string, std::string>> tags;    // ID 标签，如 ti/ar/al/by
    int64_t offsetMs = 0;                                     // [offset:] 值，毫秒
    bool synced = false;                                      // 是否含有效时标行
};

}  // namespace openlyrics
```

- [ ] **Step 2: 写解析器声明**

`extensions/foo_openlyrics_mac/core/parser/LrcParser.h`
```cpp
#pragma once
#include "model/LyricData.h"
#include <string>

namespace openlyrics {

class LrcParser {
public:
    // 解析 LRC 或纯文本。含任一时标行则 synced=true，
    // 时标行按 timeMs 升序；纯文本行 timeMs=-1 并保留原始顺序。
    static LyricData parse(const std::string& text);
};

}  // namespace openlyrics
```

- [ ] **Step 3: 写首个失败测试（单时标 + 纯文本回退）**

`tests/test_lrc_parser.cpp`
```cpp
#include <gtest/gtest.h>
#include "parser/LrcParser.h"

using namespace openlyrics;

TEST(LrcParser, SingleTimestampLine) {
    LyricData d = LrcParser::parse("[00:12.34]Hello world");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_TRUE(d.synced);
    EXPECT_EQ(d.lines[0].timeMs, 12340);
    EXPECT_EQ(d.lines[0].text, "Hello world");
}

TEST(LrcParser, PlainTextFallback) {
    LyricData d = LrcParser::parse("just a line\nanother line");
    ASSERT_EQ(d.lines.size(), 2u);
    EXPECT_FALSE(d.synced);
    EXPECT_EQ(d.lines[0].timeMs, -1);
    EXPECT_EQ(d.lines[0].text, "just a line");
    EXPECT_EQ(d.lines[1].text, "another line");
}
```

- [ ] **Step 4: 把新文件接入 CMake**

在 `CMakeLists.txt` 中把 `openlyrics_core` 由 INTERFACE 改为静态库，并加入解析器源与测试。将
```cmake
add_library(openlyrics_core INTERFACE)
target_include_directories(openlyrics_core INTERFACE extensions/foo_openlyrics_mac/core)
```
改为
```cmake
add_library(openlyrics_core STATIC
  extensions/foo_openlyrics_mac/core/parser/LrcParser.cpp
)
target_include_directories(openlyrics_core PUBLIC extensions/foo_openlyrics_mac/core)
```
并把测试可执行文件的源列表由
```cmake
add_executable(core_tests
  tests/test_smoke.cpp
)
```
改为
```cmake
add_executable(core_tests
  tests/test_smoke.cpp
  tests/test_lrc_parser.cpp
)
```

- [ ] **Step 5: 运行确认失败**

Run:
```bash
cmake -S . -B build && cmake --build build 2>&1 | tail -5
```
Expected: 链接失败，报 `LrcParser::parse` 未定义（`.cpp` 尚未实现）。

- [ ] **Step 6: 写解析器实现**

`extensions/foo_openlyrics_mac/core/parser/LrcParser.cpp`
```cpp
#include "parser/LrcParser.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace openlyrics {
namespace {

// 尝试把 [xx:yy.zz] / [xx:yy] 解析为毫秒。成功返回 true 并写 outMs。
bool parseTimeTag(const std::string& body, int64_t& outMs) {
    // body 形如 "00:12.34" 或 "00:12" 或 "00:12.345"
    size_t colon = body.find(':');
    if (colon == std::string::npos) return false;
    std::string mm = body.substr(0, colon);
    std::string rest = body.substr(colon + 1);
    if (mm.empty() || rest.empty()) return false;
    for (char c : mm) if (!std::isdigit((unsigned char)c)) return false;

    std::string ss = rest, frac;
    size_t dot = rest.find('.');
    if (dot != std::string::npos) {
        ss = rest.substr(0, dot);
        frac = rest.substr(dot + 1);
    }
    if (ss.size() != 2) return false;
    for (char c : ss) if (!std::isdigit((unsigned char)c)) return false;
    for (char c : frac) if (!std::isdigit((unsigned char)c)) return false;

    int64_t minutes = std::stoll(mm);
    int64_t seconds = std::stoll(ss);
    int64_t fracMs = 0;
    if (!frac.empty()) {
        // 归一到毫秒：补齐/截断到 3 位
        std::string f3 = (frac + "000").substr(0, 3);
        fracMs = std::stoll(f3);
    }
    outMs = (minutes * 60 + seconds) * 1000 + fracMs;
    return true;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

}  // namespace

LyricData LrcParser::parse(const std::string& text) {
    LyricData data;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();

        // 收集本行开头连续的 [] 标签
        std::vector<int64_t> times;
        size_t pos = 0;
        bool consumed = false;
        while (pos < raw.size() && raw[pos] == '[') {
            size_t close = raw.find(']', pos);
            if (close == std::string::npos) break;
            std::string body = raw.substr(pos + 1, close - pos - 1);
            int64_t ms = 0;
            if (parseTimeTag(body, ms)) {
                times.push_back(ms);
            } else {
                // 可能是 id 标签，形如 key:value
                size_t c = body.find(':');
                if (c != std::string::npos) {
                    std::string key = body.substr(0, c);
                    std::string val = body.substr(c + 1);
                    if (key == "offset") {
                        data.offsetMs = std::stoll(trim(val));
                    } else {
                        data.tags.emplace_back(key, val);
                    }
                }
            }
            pos = close + 1;
            consumed = true;
        }

        std::string content = raw.substr(pos);
        if (!times.empty()) {
            for (int64_t t : times) {
                LyricLine line;
                line.timeMs = t;
                line.text = content;
                data.lines.push_back(line);
            }
        } else if (!consumed) {
            // 无任何标签，纯文本行（含空行）
            LyricLine line;
            line.timeMs = -1;
            line.text = content;
            data.lines.push_back(line);
        }
        // consumed 但无 time（纯 id 标签行）不产出歌词行
    }

    data.synced = std::any_of(data.lines.begin(), data.lines.end(),
                              [](const LyricLine& l) { return l.timeMs >= 0; });

    if (data.synced) {
        std::stable_sort(data.lines.begin(), data.lines.end(),
                         [](const LyricLine& a, const LyricLine& b) {
                             return a.timeMs < b.timeMs;
                         });
    }
    return data;
}

}  // namespace openlyrics
```

- [ ] **Step 7: 运行确认通过**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure -R LrcParser
```
Expected: `LrcParser.SingleTimestampLine`、`LrcParser.PlainTextFallback` 均 PASS。

- [ ] **Step 8: 提交**

```bash
git add extensions/foo_openlyrics_mac/core/model/LyricData.h \
        extensions/foo_openlyrics_mac/core/parser/LrcParser.h \
        extensions/foo_openlyrics_mac/core/parser/LrcParser.cpp \
        tests/test_lrc_parser.cpp CMakeLists.txt
git commit -m "实现歌词模型与 LRC 基础解析"
```

---

### Task 3: LrcParser 进阶（多时标、offset、id 标签、畸形输入）

**Files:**
- Modify: `tests/test_lrc_parser.cpp`

**Interfaces:**
- Consumes: `LrcParser::parse`（Task 2）
- Produces: 无新接口，补齐解析器行为覆盖

- [ ] **Step 1: 追加进阶测试**

在 `tests/test_lrc_parser.cpp` 末尾追加
```cpp
TEST(LrcParser, MultipleTimestampsExpandToLines) {
    LyricData d = LrcParser::parse("[00:01.00][00:03.00]repeat");
    ASSERT_EQ(d.lines.size(), 2u);
    EXPECT_EQ(d.lines[0].timeMs, 1000);
    EXPECT_EQ(d.lines[1].timeMs, 3000);
    EXPECT_EQ(d.lines[0].text, "repeat");
    EXPECT_EQ(d.lines[1].text, "repeat");
}

TEST(LrcParser, OffsetTagParsed) {
    LyricData d = LrcParser::parse("[offset:-500]\n[00:02.00]line");
    EXPECT_EQ(d.offsetMs, -500);
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].timeMs, 2000);
}

TEST(LrcParser, IdTagsCollected) {
    LyricData d = LrcParser::parse("[ti:Song]\n[ar:Artist]\n[00:00.00]x");
    ASSERT_EQ(d.tags.size(), 2u);
    EXPECT_EQ(d.tags[0].first, "ti");
    EXPECT_EQ(d.tags[0].second, "Song");
    EXPECT_EQ(d.tags[1].first, "ar");
    EXPECT_EQ(d.tags[1].second, "Artist");
}

TEST(LrcParser, LinesSortedAscending) {
    LyricData d = LrcParser::parse("[00:05.00]b\n[00:01.00]a");
    ASSERT_EQ(d.lines.size(), 2u);
    EXPECT_EQ(d.lines[0].text, "a");
    EXPECT_EQ(d.lines[1].text, "b");
}

TEST(LrcParser, MalformedBracketTreatedAsText) {
    LyricData d = LrcParser::parse("[not a time]still text");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_FALSE(d.synced);
    EXPECT_EQ(d.lines[0].timeMs, -1);
    // [not a time] 含冒号，被当作 id 标签 key="not a time"，正文为剩余
    EXPECT_EQ(d.lines[0].text, "still text");
}

TEST(LrcParser, EmptyInput) {
    LyricData d = LrcParser::parse("");
    EXPECT_TRUE(d.lines.empty());
    EXPECT_FALSE(d.synced);
}

TEST(LrcParser, ThreeDigitFraction) {
    LyricData d = LrcParser::parse("[00:01.005]x");
    ASSERT_EQ(d.lines.size(), 1u);
    EXPECT_EQ(d.lines[0].timeMs, 1005);
}
```

- [ ] **Step 2: 运行**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure -R LrcParser
```
Expected: 全部 PASS。若 `ThreeDigitFraction` 或 `MalformedBracketTreatedAsText` 失败，按 Task 2 实现的对应分支修正后重跑（实现已覆盖这两种情形，预期直接通过）。

- [ ] **Step 3: 提交**

```bash
git add tests/test_lrc_parser.cpp
git commit -m "补齐 LRC 解析进阶用例覆盖"
```

---

### Task 4: LrcSerializer 序列化

**Files:**
- Create: `extensions/foo_openlyrics_mac/core/parser/LrcSerializer.h`
- Create: `extensions/foo_openlyrics_mac/core/parser/LrcSerializer.cpp`
- Create: `tests/test_lrc_serializer.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `LyricData`（Task 2）
- Produces: `static std::string openlyrics::LrcSerializer::serialize(const LyricData& data);`

- [ ] **Step 1: 写声明**

`extensions/foo_openlyrics_mac/core/parser/LrcSerializer.h`
```cpp
#pragma once
#include "model/LyricData.h"
#include <string>

namespace openlyrics {

class LrcSerializer {
public:
    // 输出标准 LRC。先写 id 标签，再写 offset（非 0 时），
    // 再按顺序写 [mm:ss.xx]text；无时标行仅写文本。
    static std::string serialize(const LyricData& data);
};

}  // namespace openlyrics
```

- [ ] **Step 2: 写失败测试**

`tests/test_lrc_serializer.cpp`
```cpp
#include <gtest/gtest.h>
#include "parser/LrcSerializer.h"
#include "parser/LrcParser.h"

using namespace openlyrics;

TEST(LrcSerializer, FormatsTimestamp) {
    LyricData d;
    d.synced = true;
    d.lines.push_back({12340, "Hello", {}});
    std::string out = LrcSerializer::serialize(d);
    EXPECT_EQ(out, "[00:12.34]Hello\n");
}

TEST(LrcSerializer, WritesTagsAndOffset) {
    LyricData d;
    d.tags = {{"ti", "Song"}};
    d.offsetMs = -500;
    d.synced = true;
    d.lines.push_back({0, "x", {}});
    std::string out = LrcSerializer::serialize(d);
    EXPECT_EQ(out, "[ti:Song]\n[offset:-500]\n[00:00.00]x\n");
}

TEST(LrcSerializer, RoundTrip) {
    std::string src = "[00:01.50]a\n[00:03.00]b\n";
    LyricData d = LrcParser::parse(src);
    EXPECT_EQ(LrcSerializer::serialize(d), src);
}

TEST(LrcSerializer, PlainTextLinesNoTimestamp) {
    LyricData d;
    d.lines.push_back({-1, "just text", {}});
    EXPECT_EQ(LrcSerializer::serialize(d), "just text\n");
}
```

- [ ] **Step 3: 接入 CMake**

在 `CMakeLists.txt` 的 `add_library(openlyrics_core STATIC ...)` 列表加入
```cmake
  extensions/foo_openlyrics_mac/core/parser/LrcSerializer.cpp
```
在 `add_executable(core_tests ...)` 列表加入
```cmake
  tests/test_lrc_serializer.cpp
```

- [ ] **Step 4: 运行确认失败**

Run:
```bash
cmake -S . -B build && cmake --build build 2>&1 | tail -5
```
Expected: 链接失败，`LrcSerializer::serialize` 未定义。

- [ ] **Step 5: 写实现**

`extensions/foo_openlyrics_mac/core/parser/LrcSerializer.cpp`
```cpp
#include "parser/LrcSerializer.h"
#include <cstdio>

namespace openlyrics {

std::string LrcSerializer::serialize(const LyricData& data) {
    std::string out;
    for (const auto& t : data.tags) {
        out += "[" + t.first + ":" + t.second + "]\n";
    }
    if (data.offsetMs != 0) {
        out += "[offset:" + std::to_string(data.offsetMs) + "]\n";
    }
    for (const auto& line : data.lines) {
        if (line.timeMs >= 0) {
            int64_t total = line.timeMs;
            int64_t minutes = total / 60000;
            int64_t seconds = (total % 60000) / 1000;
            int64_t hundredths = (total % 1000) / 10;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "[%02lld:%02lld.%02lld]",
                          (long long)minutes, (long long)seconds, (long long)hundredths);
            out += buf;
        }
        out += line.text;
        out += "\n";
    }
    return out;
}

}  // namespace openlyrics
```

- [ ] **Step 6: 运行确认通过**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure -R LrcSerializer
```
Expected: 四个用例全 PASS。

- [ ] **Step 7: 提交**

```bash
git add extensions/foo_openlyrics_mac/core/parser/LrcSerializer.h \
        extensions/foo_openlyrics_mac/core/parser/LrcSerializer.cpp \
        tests/test_lrc_serializer.cpp CMakeLists.txt
git commit -m "实现 LRC 序列化与往返一致"
```

---

### Task 5: SyncEngine 播放同步引擎

**Files:**
- Create: `extensions/foo_openlyrics_mac/core/sync/SyncEngine.h`
- Create: `extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp`
- Create: `tests/test_sync_engine.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `LyricData`（Task 2）
- Produces:
  - `struct openlyrics::SyncResult { int lineIndex; double progress; };`（`lineIndex == -1` 表示在首个时标行之前；`progress` 为当前行到下一行的插值 `[0,1)`，末行恒为 0）
  - `static SyncResult openlyrics::SyncEngine::locate(const LyricData& data, int64_t positionMs, int64_t extraOffsetMs = 0);`
  - offset 语义。有效比较位置 `eff = positionMs + data.offsetMs + extraOffsetMs`，取满足 `timeMs <= eff` 的最后一个时标行。正 offset 使歌词提前显示。仅对 `timeMs >= 0` 的行参与定位。

- [ ] **Step 1: 写声明**

`extensions/foo_openlyrics_mac/core/sync/SyncEngine.h`
```cpp
#pragma once
#include "model/LyricData.h"
#include <cstdint>

namespace openlyrics {

struct SyncResult {
    int lineIndex = -1;     // -1 表示尚未到达首个时标行
    double progress = 0.0;  // 当前行到下一行的插值进度 [0,1)，末行为 0
};

class SyncEngine {
public:
    // data.lines 需为按 timeMs 升序的行（LrcParser 已保证）。
    // 无时标数据（synced=false）恒返回 {-1, 0}。
    static SyncResult locate(const LyricData& data, int64_t positionMs,
                             int64_t extraOffsetMs = 0);
};

}  // namespace openlyrics
```

- [ ] **Step 2: 写失败测试**

`tests/test_sync_engine.cpp`
```cpp
#include <gtest/gtest.h>
#include "sync/SyncEngine.h"

using namespace openlyrics;

static LyricData makeData() {
    LyricData d;
    d.synced = true;
    d.lines.push_back({1000, "a", {}});
    d.lines.push_back({3000, "b", {}});
    d.lines.push_back({5000, "c", {}});
    return d;
}

TEST(SyncEngine, BeforeFirstLine) {
    SyncResult r = SyncEngine::locate(makeData(), 500);
    EXPECT_EQ(r.lineIndex, -1);
    EXPECT_DOUBLE_EQ(r.progress, 0.0);
}

TEST(SyncEngine, ExactBoundaryIsCurrentLine) {
    SyncResult r = SyncEngine::locate(makeData(), 3000);
    EXPECT_EQ(r.lineIndex, 1);
    EXPECT_DOUBLE_EQ(r.progress, 0.0);
}

TEST(SyncEngine, MidwayProgress) {
    SyncResult r = SyncEngine::locate(makeData(), 2000);  // 在 a(1000) 与 b(3000) 之间
    EXPECT_EQ(r.lineIndex, 0);
    EXPECT_DOUBLE_EQ(r.progress, 0.5);
}

TEST(SyncEngine, LastLineProgressZero) {
    SyncResult r = SyncEngine::locate(makeData(), 9000);
    EXPECT_EQ(r.lineIndex, 2);
    EXPECT_DOUBLE_EQ(r.progress, 0.0);
}

TEST(SyncEngine, PositiveOffsetAdvances) {
    // extraOffset=+1000，eff=1500+1000=2500，落在 a(1000)
    SyncResult r = SyncEngine::locate(makeData(), 1500, 1000);
    EXPECT_EQ(r.lineIndex, 0);
}

TEST(SyncEngine, DataOffsetApplied) {
    LyricData d = makeData();
    d.offsetMs = -2000;                       // eff = 3000 + (-2000) = 1000 -> a
    SyncResult r = SyncEngine::locate(d, 3000);
    EXPECT_EQ(r.lineIndex, 0);
}

TEST(SyncEngine, UnsyncedReturnsNoLine) {
    LyricData d;
    d.synced = false;
    d.lines.push_back({-1, "plain", {}});
    SyncResult r = SyncEngine::locate(d, 5000);
    EXPECT_EQ(r.lineIndex, -1);
}
```

- [ ] **Step 3: 接入 CMake**

在 `add_library(openlyrics_core STATIC ...)` 列表加入
```cmake
  extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp
```
在 `add_executable(core_tests ...)` 列表加入
```cmake
  tests/test_sync_engine.cpp
```

- [ ] **Step 4: 运行确认失败**

Run:
```bash
cmake -S . -B build && cmake --build build 2>&1 | tail -5
```
Expected: 链接失败，`SyncEngine::locate` 未定义。

- [ ] **Step 5: 写实现**

`extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp`
```cpp
#include "sync/SyncEngine.h"

namespace openlyrics {

SyncResult SyncEngine::locate(const LyricData& data, int64_t positionMs,
                              int64_t extraOffsetMs) {
    SyncResult result;
    if (!data.synced) return result;

    const int64_t eff = positionMs + data.offsetMs + extraOffsetMs;

    // 找满足 timeMs <= eff 的最后一个时标行
    int current = -1;
    for (int i = 0; i < (int)data.lines.size(); ++i) {
        if (data.lines[i].timeMs < 0) continue;      // 跳过无时标行
        if (data.lines[i].timeMs <= eff) {
            current = i;
        } else {
            break;                                   // 已升序，后面更大
        }
    }
    result.lineIndex = current;
    if (current < 0) return result;

    // 找下一个有时标的行以算插值
    int next = -1;
    for (int j = current + 1; j < (int)data.lines.size(); ++j) {
        if (data.lines[j].timeMs >= 0) { next = j; break; }
    }
    if (next >= 0) {
        int64_t start = data.lines[current].timeMs;
        int64_t end = data.lines[next].timeMs;
        if (end > start) {
            double p = double(eff - start) / double(end - start);
            if (p < 0.0) p = 0.0;
            if (p >= 1.0) p = 0.999999;
            result.progress = p;
        }
    }
    return result;
}

}  // namespace openlyrics
```

- [ ] **Step 6: 运行确认通过**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure -R SyncEngine
```
Expected: 七个用例全 PASS。

- [ ] **Step 7: 提交**

```bash
git add extensions/foo_openlyrics_mac/core/sync/SyncEngine.h \
        extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp \
        tests/test_sync_engine.cpp CMakeLists.txt
git commit -m "实现播放同步引擎与 offset 语义"
```

---

### Task 6: 平台端口抽象接口

**Files:**
- Create: `extensions/foo_openlyrics_mac/core/ports/HttpClient.h`
- Create: `extensions/foo_openlyrics_mac/core/ports/FileSystem.h`
- Create: `extensions/foo_openlyrics_mac/core/ports/TagIO.h`
- Create: `extensions/foo_openlyrics_mac/core/ports/Clock.h`
- Create: `tests/test_ports.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: 无
- Produces（供计划二起的 Layer B 实现、以及计划三的 SearchPipeline 注入使用）:
  - `struct openlyrics::TrackMeta { std::string artist, title, album, path; int64_t lengthMs = 0; };`
  - `struct openlyrics::HttpResponse { int status = 0; std::string body; };`
  - `class HttpClient { virtual HttpResponse get(const std::string& url, const std::vector<std::pair<std::string,std::string>>& headers = {}) = 0; ... };`
  - `class FileSystem { virtual bool readFile(const std::string& path, std::string& out) = 0; virtual bool writeFile(const std::string& path, const std::string& data) = 0; virtual bool exists(const std::string& path) = 0; ... };`
  - `class TagIO { virtual bool readLyricTag(const TrackMeta& t, std::string& out) = 0; virtual bool writeLyricTag(const TrackMeta& t, const std::string& lrc) = 0; ... };`
  - `class Clock { virtual int64_t nowMs() = 0; ... };`

- [ ] **Step 1: 写 TrackMeta 与 HttpClient 端口**

`extensions/foo_openlyrics_mac/core/ports/HttpClient.h`
```cpp
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace openlyrics {

struct TrackMeta {
    std::string artist;
    std::string title;
    std::string album;
    std::string path;
    int64_t lengthMs = 0;
};

struct HttpResponse {
    int status = 0;      // HTTP 状态码，0 表示传输失败
    std::string body;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse get(
        const std::string& url,
        const std::vector<std::pair<std::string, std::string>>& headers = {}) = 0;
};

}  // namespace openlyrics
```

- [ ] **Step 2: 写 FileSystem 端口**

`extensions/foo_openlyrics_mac/core/ports/FileSystem.h`
```cpp
#pragma once
#include <string>

namespace openlyrics {

class FileSystem {
public:
    virtual ~FileSystem() = default;
    virtual bool exists(const std::string& path) = 0;
    virtual bool readFile(const std::string& path, std::string& out) = 0;
    virtual bool writeFile(const std::string& path, const std::string& data) = 0;
};

}  // namespace openlyrics
```

- [ ] **Step 3: 写 TagIO 端口**

`extensions/foo_openlyrics_mac/core/ports/TagIO.h`
```cpp
#pragma once
#include "ports/HttpClient.h"  // 复用 TrackMeta
#include <string>

namespace openlyrics {

class TagIO {
public:
    virtual ~TagIO() = default;
    // 读内嵌歌词标签，成功写 out 并返回 true
    virtual bool readLyricTag(const TrackMeta& track, std::string& out) = 0;
    // 写回内嵌歌词标签
    virtual bool writeLyricTag(const TrackMeta& track, const std::string& lrc) = 0;
};

}  // namespace openlyrics
```

- [ ] **Step 4: 写 Clock 端口**

`extensions/foo_openlyrics_mac/core/ports/Clock.h`
```cpp
#pragma once
#include <cstdint>

namespace openlyrics {

class Clock {
public:
    virtual ~Clock() = default;
    virtual int64_t nowMs() = 0;
};

}  // namespace openlyrics
```

- [ ] **Step 5: 写测试（用假实现验证接口可被继承与调用）**

`tests/test_ports.cpp`
```cpp
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
                     const std::vector<std::pair<std::string, std::string>>&) override {
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
```

- [ ] **Step 6: 接入 CMake**

端口均为纯头文件，无需加入库源。仅在 `add_executable(core_tests ...)` 列表加入
```cmake
  tests/test_ports.cpp
```

- [ ] **Step 7: 运行确认通过**

Run:
```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: 端口四个用例 PASS，且既有全部用例仍 PASS（全绿）。

- [ ] **Step 8: 提交**

```bash
git add extensions/foo_openlyrics_mac/core/ports/ tests/test_ports.cpp CMakeLists.txt
git commit -m "定义平台端口抽象接口与假实现测试"
```

---

## 计划一自查

**Spec coverage（对照设计文档第 5.1、11 节）**
- `LyricData` 模型 → Task 2。
- `LrcParser` / `LrcSerializer` → Task 2、3、4。
- `SyncEngine` → Task 5。
- ports（HttpClient / FileSystem / TagIO / Clock）→ Task 6。
- 命令行独立测试目标 → Task 1 起全程 GoogleTest + ctest。
- 未纳入本计划的核心模块（`LyricSource` 及五个实现、`SourceRegistry`、`SearchPipeline`、`LyricStore`）依赖 Http/Tag 端口的真实实现与 provider 样例，放入计划三，符合分解决策。

**Placeholder scan** 无 TBD/TODO，所有代码步骤均含完整可编译代码与可运行命令。

**Type consistency** `LyricData`、`LyricLine`、`Syllable`、`SyncResult`、`TrackMeta`、`HttpResponse` 字段与方法签名在各任务间一致；`TagIO.h` 复用 `HttpClient.h` 的 `TrackMeta`，无重复定义。

---

## 后续计划路线图（各自展开为独立 bite-sized 计划）

每个计划均产出可运行可测的软件，且只有在其输入到位后才展开为逐步 TDD，避免臆造未核实的 SDK 接口。

**计划二 SDK 骨架与展示闭环（对应 P0 + P1）**
前置输入。下载并解压 `SDK-2025-03-07` 到仓库；对照真实头文件核实 `ui_element_mac`、`play_callback`、metadb/file_info、mac 配置页类的确切签名。
交付。Xcode 组件工程（Ruby 生成 + `build.sh`）、`ComponentEntry` 注册、空 `OpenLyricsUIElement` 面板可加入布局、`PlaybackBridge` 打通曲目与位置、`TagIOAdapter` + `FileSystemAdapter` 实现端口、`TagSource` + `LocalFileSource`、`LyricView` 整行高亮平滑滚动。判据。带内嵌或本地 LRC 的曲目能在面板内同步高亮滚动。

**计划三 在线拉取与自动保存（对应 P2）**
前置输入。LrcLib 接口文档核实。
交付。`HttpAdapter`（NSURLSession）实现端口、`LrcLibProvider`、`SourceRegistry`、`SearchPipeline`（降级短路）、`LyricStore`（命名/目录模板、写本地或写回 tag）。判据。无本地歌词时从 LrcLib 拉取并自动落盘，下次命中本地。

**计划四 中文歌词源（对应 P3）**
前置输入。NetEase、QQ 的请求签名/加密与样例响应（录制为离线测试夹具）。
交付。`NetEaseProvider`、`QQProvider`，可插拔、可整源禁用、失效隔离。判据。两源可独立启停，失效不影响整体管线。

**计划五 手动搜索与偏移微调（对应 P4）**
交付。`SearchDialogController` 候选列表、`OffsetControl` 实时 offset 微调并持久化。判据。可手动搜索选定应用，可实时调 offset 并持久化。

**计划六 内置编辑器与配置页（对应 P5 + P6）**
交付。`LyricEditorController` 面板内编辑文本与时标并保存、`PreferencesPageProvider` 承载全部配置项。判据。面板内可编辑保存；全部配置项可在偏好设置页调整并生效。
