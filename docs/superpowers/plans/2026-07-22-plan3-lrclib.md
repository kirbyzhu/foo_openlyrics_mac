# 计划三 在线拉取与自动保存（LrcLib） Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** 当内嵌标签与本地文件都没有歌词时，自动从 LrcLib 按艺术家/标题/专辑/时长拉取歌词，在面板同步显示，并把结果自动落盘为 `<音频名>.lrc`，下次直接本地命中。

**Architecture:** 消费计划一端口 `HttpClient`/`FileSystem` 与 `LrcParser`。新增：平台层 `HttpAdapter`（NSURLSession 实现 `HttpClient`）；纯 C++ 的 `JsonField`（对象级字段提取+反转义）、`UrlEncode`、`LrcLibProvider`（实现 `LyricSource`）、`LyricStore`（落盘）。面板接线由“Tag→本地”扩为“Tag→本地→（未命中再）LrcLib→命中即落盘”。

**Tech Stack:** Objective-C++（NSURLSession）、纯 C++17、CMake、GoogleTest。不引第三方库（JSON 自写小提取器）。

## Global Constraints

- 纯 C++ 核心（`core/`）仅 std-lib，命名空间 `openlyrics`，禁含 SDK/AppKit/Objective-C。平台层 Obj-C++ ARC，类后缀 `_foo_openlyrics_mac`。
- 计划一端口 `HttpClient::get(const std::string& url, const std::vector<std::pair<std::string,std::string>>& headers) -> HttpResponse{int status; std::string body;}`；`FileSystem::readFile/writeFile/listDirectory`。
- 不破坏既有 63 项测试与 `fb2k_sdk`/`foo_openlyrics` 目标。cmake 在 /opt/homebrew/bin，CLT clang。
- 提交信息简体中文动宾式。UI/网络层人工验证。

## 已核实地面真相（LrcLib，实测 2026-07-22）

- `GET https://lrclib.net/api/get?artist_name=<enc>&track_name=<enc>&album_name=<enc>&duration=<秒>`。参数需 URL 百分号编码；`album_name`/`duration` 可选。
- 命中 200，响应 JSON 顶层对象含键：`id,name,trackName,artistName,albumName,duration,instrumental,plainLyrics,syncedLyrics,lyricsfile`。`syncedLyrics` 为 `[mm:ss.xx]` 带时标 LRC；`plainLyrics` 无时标；`instrumental` 为布尔。
- 未找到返回 **404**。无需鉴权。应带 `User-Agent` 标识（如 `foo_openlyrics_mac/0.1.0 (+https://github.com)`）。
- 拉取策略：`instrumental==true` → 视为无歌词；否则优先 `syncedLyrics`（非空），退而 `plainLyrics`（非空），都空则未命中。

## 设计决策（本计划固定）

- **落盘只针对在线结果**，写**原始 LRC 文本**（无损，保留逐字/精度）。为此 `LyricData` 增字段 `std::string sourceText`，由 `LrcParser::parse` 设为其输入原文；`LyricStore` 落盘 `sourceText`。
- 落盘目标为音频同目录 `<音频 basename>.lrc`（下次 `LocalFileSource` 精确命中）。
- 面板接线分级编排（非笼统 SearchPipeline）：先本地管线（Tag+Local）命中即用；未命中再 `LrcLibProvider`；在线命中则先 `LyricStore.save` 再显示。网络在后台队列，`HttpAdapter.get` 同步阻塞该后台线程。

---

### Task 1: LyricData 增 sourceText 并由解析器回填（纯 C++，TDD）

**Files:** Modify `core/model/LyricData.h`、`core/parser/LrcParser.cpp`；Modify `tests/test_lrc_parser.cpp`

**Interfaces:** `LyricData` 增 `std::string sourceText;`（默认空）。`LrcParser::parse(text)` 返回的 `LyricData.sourceText == text`。

- [ ] Step 1: 失败测试：`parse("[00:01.00]a").sourceText == "[00:01.00]a"`；`parse("").sourceText == ""`。
- [ ] Step 2: `LyricData.h` 加字段；`LrcParser::parse` 结尾 `data.sourceText = text;`。
- [ ] Step 3: 构建跑测试，全套仍绿（既有断言不受影响）。提交。

**判据：** LyricData 携带原文，供后续无损落盘；无回归。

### Task 2: UrlEncode（纯 C++，TDD）

**Files:** Create `core/net/UrlEncode.h/.cpp`；Create `tests/test_url_encode.cpp`；Modify `CMakeLists.txt`

**Interfaces:** `std::string openlyrics::urlEncodeComponent(const std::string& s);` —— RFC 3986 unreserved（`A-Za-z0-9-_.~`）保留，其余字节（含空格、UTF-8 多字节、`&=?`）逐字节百分号编码为 `%XX`（大写十六进制）。

- [ ] TDD 覆盖：空格→`%20`；`&`→`%26`；中文（如 `伍佰`）逐 UTF-8 字节编码；unreserved 原样；已含 `%` 的普通字符正常编码。实现→绿→提交。

**判据：** 查询参数编码正确，中文与特殊字符安全。

### Task 3: JsonField 提取器（纯 C++，TDD）

**Files:** Create `core/net/JsonField.h/.cpp`；Create `tests/test_json_field.cpp`；Modify `CMakeLists.txt`

**Interfaces（对 LrcLib 这类扁平 JSON 对象顶层取值）：**
- `bool openlyrics::jsonGetString(const std::string& json, const std::string& key, std::string& out);` —— 找到顶层 `"key"`，解析其字符串值并反转义（`\" \\ \/ \n \r \t \b \f \uXXXX`，代理对可选，未识别转义原样保留反斜杠后字符）；键不存在或值非字符串返回 false。
- `bool openlyrics::jsonGetBool(const std::string& json, const std::string& key, bool& out);` —— 值为 `true`/`false`。
- 顶层扫描须正确跳过字符串值内部（含转义引号）与嵌套 `{}`/`[]`，避免把值内部出现的同名子串误当键。

- [ ] TDD 覆盖：取普通字符串；含 `\n` 的值（如 `"[00:01]a\n[00:02]b"` → 反转义为真实换行）；含转义引号 `\"`；`\uXXXX`（如 `’`）；布尔 `instrumental`；键在值内部出现不误匹配（如某字符串值里含 `"syncedLyrics"` 字样）；缺键返回 false；`null` 值返回 false。实现→绿→提交。

**判据：** 能从 LrcLib 响应稳健取出 `syncedLyrics`/`plainLyrics`/`instrumental`。

### Task 4: LrcLibProvider（纯 C++，TDD 注入假 HttpClient）

**Files:** Create `core/sources/LrcLibProvider.h/.cpp`；Create `tests/test_lrclib_provider.cpp`；Modify `CMakeLists.txt`

**Interfaces:** `LrcLibProvider(HttpClient& http)`；实现 `LyricSource::fetch(const TrackMeta& t, LyricData& out)`：
- 拼 URL：`https://lrclib.net/api/get?artist_name=<enc(t.artist)>&track_name=<enc(t.title)>`，`t.album` 非空追加 `&album_name=<enc>`，`t.lengthMs>0` 追加 `&duration=<lengthMs/1000>`。`t.title` 为空直接返回 false。
- `http.get(url, {})`；`status==200` 时用 `JsonField`：`instrumental==true`→返回 false；否则 `syncedLyrics` 非空→`LrcParser::parse` 填 out 返回 true；否则 `plainLyrics` 非空→parse 返回 true；否则 false。`status!=200`（含 404、0 网络失败）返回 false。
- 解析后 out.sourceText 已由 parser 填为对应原始 lrc 文本（供落盘）。

- [ ] TDD（FakeHttp 返回预置 JSON）：synced 命中；仅 plain 命中；instrumental 跳过；404 未命中；网络失败(status 0)未命中；title 空直接 false；URL 含正确编码后的 artist/title/album/duration（断言 FakeHttp 收到的 url）。实现→绿→提交。

**判据：** 给定 LrcLib 响应能正确产出 LyricData（含 sourceText），错误/无歌词稳健返回 false。

### Task 5: LyricStore 落盘（纯 C++，TDD 注入假 FileSystem）

**Files:** Create `core/store/LyricStore.h/.cpp`；Create `tests/test_lyric_store.cpp`；Modify `CMakeLists.txt`

**Interfaces:** `LyricStore(FileSystem& fs)`；`bool save(const TrackMeta& track, const LyricData& data)`：
- `data.sourceText` 为空→不写，返回 false。
- 目标路径 = `track.path` 去扩展名 + `.lrc`（复用与 `LocalFileSource::stripExtension` 相同的“仅末段扩展名”规则；可将该规则提取为共享小函数或各自实现，保持一致）。
- `fs.writeFile(target, data.sourceText)` 返回其结果。

- [ ] TDD（FakeFs）：正常落盘写到 `<basename>.lrc` 且内容==sourceText；sourceText 空不写返回 false；path 无扩展名时追加 `.lrc`；写入被 FakeFs 记录可回读。实现→绿→提交。

**判据：** 在线结果无损落盘到音频同目录，下次 LocalFileSource 精确命中。

### Task 6: HttpAdapter + 面板接线（平台/GUI 验证）

**Files:** Create `platform/HttpAdapter.h/.mm`（实现 `HttpClient`）；Modify `ui/LyricPanelController.mm`（接线）；Modify `CMakeLists.txt`

**Interfaces:** `HttpAdapter` 实现 `HttpClient::get`：用 `NSURLSession` 发同步 GET（`dispatch_semaphore` 等待），默认注入 `User-Agent: foo_openlyrics_mac/0.1.0 (+https://github.com)`（若调用方未提供），超时约 10s；返回 `HttpResponse{status=HTTP码或0, body=UTF-8}`。仅在后台队列调用（切勿主线程）。

- [ ] Step 1: `HttpAdapter` 实现（NSURLSession + semaphore + UA + 超时 + 错误→status 0）。
- [ ] Step 2: 面板接线：曲目切换后台队列内——先本地管线（TagSource(TagIOAdapter)+LocalFileSource(FileSystemAdapter) 经 SearchPipeline）；未命中则 `LrcLibProvider(HttpAdapter)` fetch；在线命中先 `LyricStore(FileSystemAdapter).save` 再回主线程交 LyricView。保留 trackRequestToken 陈旧结果丢弃。无歌词占位。状态行提示“在线获取中…/未找到”。
- [ ] Step 3: 构建+签名+安装；core 单测（含新增）全绿。
- [ ] Step 4: 提交。
- [ ] Step 5: 人工验证：播一首**本地/内嵌都没有歌词**、但 LrcLib 有的曲目（英文歌较稳，如带正确 artist/title 标签者）→ 面板显示同步歌词；音频同目录出现新 `<音频名>.lrc`；断网时不卡界面、显示未找到。（subagent 构建安装后 DONE_WITH_CONCERNS 交回人验证。）

**判据：** 无本地歌词时自动从 LrcLib 拉取显示并落盘，下次本地命中；网络异常优雅降级。

---

## 自查

- 覆盖设计文档计划三要点：HttpAdapter、LrcLibProvider、SearchPipeline（本地段复用 Plan 2 的 SearchPipeline，在线段由接线编排）、LyricStore 自动保存。
- 纯 C++ 件（Task 1-5）均可脱离 foobar TDD；平台/网络（Task 6）人工验证。
- 无占位符；类型/签名跨任务一致（HttpClient/FileSystem 端口、LyricData.sourceText、LyricSource.fetch）。

## 风险

- LrcLib 匹配依赖曲目标签质量（artist/title）；标签差则可能拉不到或拉错，属数据问题，非本计划缺陷。可后续加 /api/search 兜底（留计划五手动搜索）。
- NSURLSession 同步阻塞后台线程需确保绝不在主线程调用（接线保证）。
- 落盘写用户音乐目录仅写 `<音频名>.lrc`。在线拉取只发生在本地未命中之后，而 `LocalFileSource` 精确步已大小写不敏感地检索过 `<basename>.lrc`，故该精确目标必不存在，`writeFile` 无覆盖用户既有文件之虞（清理已移除 `exists`，此处也无需存在性检查）。若用户目录另有模糊同名 .lrc，落盘的精确名文件与之并存，下次本地精确步优先命中新文件。
