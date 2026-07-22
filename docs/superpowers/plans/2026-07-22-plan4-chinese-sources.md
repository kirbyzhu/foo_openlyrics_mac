# 计划四 中文源（NetEase / QQ 音乐）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** 当 LrcLib 也找不到歌词时（中文曲目覆盖率不足），依次从网易云音乐和 QQ 音乐拉取歌词，与现有三级管线无缝衔接，结果自动落盘。

**Architecture:** 新增 `CryptoPort`（加密原语端口，core/ports/），平台层 `CryptoAdapter`（CommonCrypto + Security.framework 实现）。两个 provider 均实现 `LyricSource` 接口，依赖注入 `HttpClient` + `CryptoPort`：先搜索拿歌曲 ID，再取歌词。网易用 weapi 加密（AES-128-CBC + RSA），QQ 用 MD5 签名 + 3DES-ECB 解密响应。面板接线扩展为 Tag→Local→LrcLib→NetEase→QQMusic 五级管线。

**Tech Stack:** 纯 C++17（core）、Objective-C++（platform，CommonCrypto/Security.framework）、CMake、GoogleTest。不引第三方库。

## Global Constraints

- 纯 C++ 核心（`extensions/foo_openlyrics_mac/core/`）仅 std-lib，命名空间 `openlyrics`，禁含 SDK/AppKit/Objective-C。平台层 Obj-C++ ARC。
- 既有端口：`HttpClient::get(url, headers) -> HttpResponse{int status; std::string body;}`；`FileSystem::readFile/writeFile/listDirectory`。
- 不破坏既有 98 项测试与 `fb2k_sdk`/`foo_openlyrics` 目标。cmake 在 `/opt/homebrew/bin/cmake`，CLT clang。
- 提交信息简体中文动宾式。UI/网络层人工验证。
- 两个 provider 的搜索步骤均需额外 HTTP 请求：搜歌→取第一候选 ID→取歌词，共两次请求。

## 已核实地面真相

### 网易云音乐（weapi）

- **搜索：** POST `https://music.163.com/weapi/search/get`（或 `cloudsearch/get`），weapi 加密 body 为 `params` + `encSecKey` 两个 form 字段。参数 JSON：`{s:"artist title", type:1, offset:0, limit:5}`。返回 JSON `result.songs[].id`。
- **歌词：** POST `https://music.163.com/weapi/song/lyric`，同上加密方式。参数 JSON：`{id:"<songId>", lv:-1, tv:-1, cs:-1}`（cs 用 cookie `os=pc`）。返回 JSON `lrc.lyric`（LRC 格式）、`tlyric.lyric`（翻译）、`nolyric`（纯音乐标记）。
- **weapi 加密流程：**
  1. 生成 16 字节随机字符串作为 AES key
  2. AES-128-CBC 加密 JSON body（key=随机串，iv=`0102030405060708`，PKCS7 padding）
  3. Base64 编码得 `params`
  4. 将随机 key 反转后用固定 RSA 公钥加密，再 hex 编码得 `encSecKey`
- **固定参数：** AES iv = `0102030405060708`；RSA 公钥（hex）为 `e = 0x10001`，modulus 取自客户端固定常量（见开源实现 `NeteaseCloudMusicApi/crypto.js`）；需要 `User-Agent: Mozilla/5.0 ...` 和 `Referer: https://music.163.com/`。
- **响应格式：** 纯 JSON（无需解密）。命中 200，`code==200` 且无 `nolyric` 标记。未找到仍 200 但 `lrc.lyric` 为空或 `nolyric==true`。
- **纯音乐判断：** `nolyric==true` 或 `lrc` 缺失 → 未命中。

### QQ 音乐

- **搜索：** GET `https://c.y.qq.com/soso/fcgi-bin/client_search_cp?w=<enc>&p=1&n=5&format=json&sign=<sign>`。需要 `User-Agent` 和 `Referer: https://y.qq.com`。返回 JSON `data.song.list[].songmid`（14 位字母数字 mid）和 `data.song.list[].songid`（数字 id）。
- **歌词：** GET `https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=<mid>&format=json&g_tk=<token>`。返回 JSON，`lyric` 字段为 **base64 编码的加密歌词**。`trans` 为翻译（同样加密）。`code==0` 命中，非 0 或 lyric 为空未命中。
- **歌词解密流程（三步）：**
  1. Base64 解码
  2. 3DES-ECB 解密（key=`!@#)(*$%123ZXC!@!@#)(NHL`，24 字节）
  3. 结果是 XML：`<LyricContent>` 内为 UTF-8 LRC 文本，或 `<Lyric_1>` 嵌套（取 content 属性再解一层）
- **签名算法（g_tk）：** 基于 cookie 中 `qqmusic_key` 或默认值的简单哈希（开源实现广泛采用）。
- **签名算法（search sign）：** 对参数串做 MD5 + 特定字符重排 + Base64。
- **注意：** QQ 音乐部分 VIP 歌曲歌词需登录 cookie 才能拉取。未登录也可覆盖大量免费曲目。

## 设计决策（本计划固定）

- **新增 `CryptoPort`**（纯 C++ 虚接口），定义 AES-128-CBC 加密、RSA 公钥加密（PKCS1 v1.5）、3DES-ECB 解密、MD5 摘要四个原语。具体参数（key/iv/padding）由调用方（provider）决定，端口只执行原语。
- **平台层 `CryptoAdapter`** 用 `CommonCrypto`（CCCrypt）+ `Security.framework`（SecKeyCreateWithData）实现。纯 C 接口，无 ObjC 对象开销，可被 core 测试间接覆盖但本身无独立单测（平台件）。
- **两个 provider 各自包含搜索+取词两步**，遵循 `LyricSource::fetch` 语义：命中返回 true 并填 `LyricData`（含 `sourceText`），否则 false。
- **`QQMusicProvider` 内置 XML 解析**——仅需提取 `<LyricContent>` 或 `<Lyric_1 content="...">` 的 LRC 文本，不引入通用 XML 库，手写最小提取器（类似 `JsonField` 的定位）。
- **搜索策略：** 取搜索结果第一项（已按相关度排序）。若搜索无结果或歌词请求失败，返回 false，由管线继续下一源。
- **面板接线扩展**：`Tag→Local→LrcLib→NetEase→QQMusic`。每个在线源命中后先 `LyricStore.save` 再显示。搜索与取词均在后端队列执行。
- **失效隔离：** 单源连续失败 N 次（如 N=5）则暂时禁用该源（本次 foobar2000 会话内），避免每次切歌都白费两次 HTTP 请求。会话重启后重置。

---

### Task 1: CryptoPort 接口定义（纯 C++，仅头文件）

**Files:** Create `extensions/foo_openlyrics_mac/core/ports/CryptoPort.h`

**Interfaces:**

```cpp
namespace openlyrics {

class CryptoPort {
public:
    virtual ~CryptoPort() = default;

    // AES-128-CBC 加密，PKCS7 padding。key/iv 为原始字节（长度 16）。
    // 返回加密后的原始字节。调用方自行做后续编码（base64/hex）。
    virtual std::string aes128CbcEncrypt(
        const std::string& plain,
        const std::string& key,
        const std::string& iv) = 0;

    // RSA 公钥加密（PKCS1 v1.5 填充）。
    // pubKeyDer: DER 编码的公钥（SubjectPublicKeyInfo）。
    // 注意：上传 RSA 公钥的 modulus+e 组装在平台层完成，端口只接收 DER。
    virtual std::string rsaPkcs1Encrypt(
        const std::string& plain,
        const std::string& pubKeyDer) = 0;

    // 3DES-ECB 解密（无 padding，调用方自行处理）。
    // key 为原始字节（长度 24）。
    virtual std::string tripleDesEcbDecrypt(
        const std::string& cipher,
        const std::string& key) = 0;

    // MD5 摘要，返回 32 字符小写 hex。
    virtual std::string md5Hex(const std::string& data) = 0;
};

}  // namespace openlyrics
```

- [ ] Step 1: 编写头文件，添加 `CryptoPort.h` 到 `core/ports/`。
- [ ] Step 2: 确认 cmake 构建无编译错误（头文件语法正确，无实现源文件需编译）。

**判据:** 端口定义清晰，四个原语覆盖网易+QQ 所有加密需求；可被 FakeCrypto 实现用于 TDD。

---

### Task 2: NetEaseProvider（纯 C++，TDD 注入 FakeHttp + FakeCrypto）

**Files:** Create `extensions/foo_openlyrics_mac/core/sources/NetEaseProvider.h/.cpp`；Create `tests/test_netease_provider.cpp`；Modify `CMakeLists.txt`

**Interfaces:** `NetEaseProvider(HttpClient& http, CryptoPort& crypto)`；实现 `LyricSource::fetch(const TrackMeta& track, LyricData& out)`：

**搜索步骤：**
1. `track.title` 为空 → 直接 false。
2. 构建搜索 JSON：`{"s":"<artist> <title>","type":1,"offset":0,"limit":5}`。
3. weapi 加密：生成随机 16 字节 key → `aes128CbcEncrypt(json, key, "\x01\x02\x03\x04\x05\x06\x07\x08")` → base64 → `params`；key 反转 → `rsaPkcs1Encrypt`（用固定网易公钥 DER）→ hex → `encSecKey`。
4. POST `https://music.163.com/weapi/search/get`，body 为 `params=<enc>&encSecKey=<enc>`（URL-encoded form），headers 含 `User-Agent`、`Referer`、`Content-Type: application/x-www-form-urlencoded`。
5. 解析响应 JSON：`jsonGetInt(r.body, "code")==200` → 取 `result.songs[0].id`（用 `JsonField` 取 `id` 值）；无歌曲或 code≠200 → false。

**歌词步骤：**
1. 构建歌词 JSON：`{"id":"<songId>","lv":-1,"tv":-1,"cs":-1}`。
2. 同 weapi 加密 POST `https://music.163.com/weapi/song/lyric`。
3. 解析响应：`code==200` 且 `nolyric` 不为 true → 取 `lrc.lyric` 字符串（LRC 文本）；`tlyric.lyric` 可选（翻译，暂不处理）。
4. `LrcParser::parse(lrcText)` 填 out（`sourceText` 自动回填），返回 true。
5. 任一异常 → false。

**weapi 固定常量：**
- AES iv: `0102030405060708`（8 字节 hex → 16 字节 key 用 `0` 补齐？实际是 16 字节 ASCII 字符串 `"0102030405060708"`）
- RSA public key modulus（hex，取自 `NeteaseCloudMusicApi`）：`00e0b509f6259df8642dbc35662901477df22677ec152b5ff68ace615bb7...`（完整的 256 字节 RSA-2048 密钥，硬编码在 provider 中，运行时组装为 DER 再传 CryptoPort）
- Cookie: `os=pc`（部分端点需要 `cs:-1` 已通过 body 参数体现）

- [ ] Step 1: FakeCrypto + FakeHttp 写失败测试：搜索返回空列表→false；歌词 API 返回 nolyric→false；title 空→false。
- [ ] Step 2: 写成功测试：搜索返回单曲 + lyrics 返回 LRC→命中并验证 LyricData.sourceText 非空。
- [ ] Step 3: 验证 weapi 加密参数正确性：FakeCrypto 记录收到的加密调用参数；FakeHttp 断言 params/encSecKey 字段存在且 Content-Type 正确。
- [ ] Step 4: 实现 `NetEaseProvider`。构建跑测试，全套仍绿（含既有 98 项）。
- [ ] Step 5: 提交。

**判据:** 给定模拟网易响应，能正确搜索+取词+解析 LRC；网络/加密错误优雅降级。

---

### Task 3: QQMusicProvider（纯 C++，TDD 注入 FakeHttp + FakeCrypto）

**Files:** Create `extensions/foo_openlyrics_mac/core/sources/QQMusicProvider.h/.cpp`；Create `tests/test_qqmusic_provider.cpp`；Modify `CMakeLists.txt`

**Interfaces:** `QQMusicProvider(HttpClient& http, CryptoPort& crypto)`；实现 `LyricSource::fetch`：

**搜索步骤：**
1. `track.title` 为空 → 直接 false。
2. 构建搜索 URL：`https://c.y.qq.com/soso/fcgi-bin/client_search_cp?w=<urlEncode(artist+title)>&p=1&n=5&format=json&sign=<sign>`。sign 算法：对参数串做 MD5 → 特定字符重排 → Base64（细节见 QQMUSIC_SIGN.md）。
3. GET 请求，headers 含 `User-Agent` 和 `Referer: https://y.qq.com`。
4. 解析响应：`code==0` → 取 `data.song.list[0].songmid`；否则 false。

**歌词步骤：**
1. 计算 g_tk（基于 cookie 或默认值，详见实现）。
2. GET `https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=<mid>&format=json&g_tk=<token>`。
3. 解析响应：`code==0` 且 `lyric` 非空 → Base64 解码 → `tripleDesEcbDecrypt` → XML 提取 LRC。
4. XML 提取逻辑（最小解析器）：
   - 查找 `<LyricContent>` 标签，取其内容作为 LRC 文本
   - 若含 `<Lyric_1 content="...">`，取 content 属性值（也是加密文本，再解一次 3DES → 嵌套 XML → 取 `<LyricContent>`）
5. `LrcParser::parse(lrcText)` 填 out，返回 true。

**QQ 音乐固定常量：**
- 3DES key: `!@#)(*$%123ZXC!@!@#)(NHL`（24 字节 ASCII）
- sign 算法常量（MD5 重排映射表）
- g_tk 默认值

- [ ] Step 1: FakeCrypto + FakeHttp 写失败测试：搜索空→false；lyric 空→false；code≠0→false。
- [ ] Step 2: 写成功测试：模拟完整搜索+歌词响应（base64 加密的 XML→3DES 解密→LRC）。
- [ ] Step 3: 验证 sign 和 g_tk 计算正确（FakeCrypto 记录 MD5 调用参数）。
- [ ] Step 4: 实现 `QQMusicProvider` + 内嵌 XML 提取器。构建跑测试，全套仍绿。
- [ ] Step 5: 提交。

**判据:** 给定模拟 QQ 音乐响应，能正确解密+解析 LRC；各种错误稳健返回 false。

---

### Task 4: CryptoAdapter（平台层，CommonCrypto + Security.framework）

**Files:** Create `extensions/foo_openlyrics_mac/platform/CryptoAdapter.h/.mm`；Modify `CMakeLists.txt`

**Interfaces:** `CryptoAdapter` 实现 `CryptoPort`：

- `aes128CbcEncrypt`: 调 `CCCrypt(kCCEncrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, ...)`。
- `rsaPkcs1Encrypt`: 从 DER 创建 `SecKeyRef`（`SecKeyCreateWithData`），再 `SecKeyEncrypt`（`kSecPaddingPKCS1`）。
- `tripleDesEcbDecrypt`: 调 `CCCrypt(kCCDecrypt, kCCAlgorithm3DES, 0 /*ECB*/, ...)`。需处理 QQ 音乐的 3DES 变体（可能需用 kCCOptionECBMode）。
- `md5Hex`: 调 `CC_MD5`，hex 编码。

- [ ] Step 1: 创建 `CryptoAdapter.h` 声明 + `CryptoAdapter.mm` 实现（纯 C API，无 ObjC 对象）。
- [ ] Step 2: 更新 `CMakeLists.txt`，`CryptoAdapter.mm` 加入 `foo_openlyrics` 目标（ARC）。
- [ ] Step 3: 构建验证（需 SDK 目录存在）。无法命令行跑加密原语单测（依赖 macOS 框架），但编译成功即 API 签名正确。
- [ ] Step 4: 提交。

**判据:** CryptoAdapter 编译通过；ARC 无泄漏警告。

---

### Task 5: 面板接线（五级管线 + 失效隔离）

**Files:** Modify `extensions/foo_openlyrics_mac/ui/LyricPanelController.mm`；Modify `extensions/foo_openlyrics_mac/platform/CMakeLists.txt`

- [ ] Step 1: `LyricPanelController` 中新增 `CryptoAdapter`、`NetEaseProvider`、`QQMusicProvider` 的持有与管理。
- [ ] Step 2: 扩展管线：Tag→Local→LrcLib→NetEase→QQMusic。每个在线源命中后先 `LyricStore.save` 再显示。
- [ ] Step 3: 实现简单失效计数器：单源连续失败 5 次则本次会话跳过该源；任意一次成功清零计数器；会话重启重置。状态行更新提示当前检索源（"正在搜索网易云…""正在搜索 QQ 音乐…"）。
- [ ] Step 4: 构建+签名+安装；core 单测全绿。
- [ ] Step 5: 提交。

**判据:** 五级管线按序检索；NetEase/QQ 命中则显示+落盘；都未命中显示占位；失效隔离生效。

---

### Task 6: 端到端人工验证

- [ ] 找一首只有网易云/QQ 音乐有歌词的中文歌（内嵌标签+本地+ LrcLib 均无）→ 面板显示同步歌词。
- [ ] 验证落盘 `.lrc` 文件在音频同目录。
- [ ] 找一首网易云和 QQ 音乐都没有的极冷门曲 → 显示"未找到"，不卡界面。
- [ ] 断网切歌 → 三个在线源依次失败但不过长时间阻塞（单个源超时约 10s，三源总计 ≤35s）。
- [ ] 重启 foobar2000 → 失效计数器重置，源恢复可用。

---

## 自查

- 计划四覆盖两个中文源、CryptoPort、CryptoAdapter、面板接线、失效隔离。
- 纯 C++ 件（Task 1-3）均可脱离 foobar TDD；平台件（Task 4）仅编译验证；面板（Task 5）人工验证。
- 不破坏既有 98 项测试。加密实现使用 macOS 系统框架（CommonCrypto/Security），零第三方依赖。
- 两个 provider 均遵循 `LyricSource::fetch` 模式，与 `LrcLibProvider` 一致。

## 风险

- **网易/QQ 接口易变。** 加密算法相对稳定（weapi 数年未改），但搜索/歌词端点 URL、参数名可能调整。若接口变更，需更新 provider 逻辑和测试夹具。
- **加密原语平台依赖。** AES/RSA/3DES 实现绑定 macOS CommonCrypto/Security，不可移植至 Windows/Linux。若后续跨平台，需补对应实现。
- **搜索+取词两步请求延迟。** 单个源最多 2 次 HTTP round-trip（搜索→取词），最坏情况两个源共 4 次。配合既有 LrcLib（1 次），五级管线最多 5 次 HTTP 请求。超时与失效隔离可限制影响。
- **QQ 音乐 XML 解析脆弱性。** QQ 音乐 lyrics XML 结构可能含多层嵌套 `<Lyric_1>`。内嵌最小解析器需覆盖常见变体，边缘格式（如罗马音 `<contentroma>`）可忽略。
- **VIP 歌曲限制。** QQ 音乐部分版权曲目需登录 cookie。不登录覆盖率仍可接受（免费曲库 + 网易兜底）。
