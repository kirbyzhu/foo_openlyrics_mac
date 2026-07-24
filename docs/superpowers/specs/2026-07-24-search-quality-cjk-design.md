# 网易云/QQ 歌词搜索精准度与召回优化设计

日期：2026-07-24
状态：已确认方向，待实现

## 背景

用户反馈网易云、QQ 音乐歌词搜索"应有却搜不到"（召回低）与"匹配到错的歌"（精准度差）。代码排查定位三处短板：

1. `Matcher::jaccardSimilarity`（Matcher.cpp:110）按空格分词，中文整句是单个 token，两个不同中文标题除非完全相同或子串包含，相似度恒为 0。中文标题只要双方各有差异（繁简、词序、别名、附加信息不同）就打分骤降，导致正确歌低于 `kLowThreshold=40` 被判"搜不到"，或被错误版本盖过。
2. 两源搜索候选写死 `limit=5`（NetEaseProvider.cpp:168、QQMusicProvider.cpp:39），热门歌多版本时正确原版可能排在第 6+，进不了候选池。
3. 搜索 query 直接用原始 `artist + " " + title`，title 含 `(feat. X)`、`(电影主题曲)`、`【Live】` 等噪声时召回下降。

`scoreText`（Matcher.cpp:128）的双向子串分支（90 分）已覆盖"一方带版本后缀、另一方是纯标题"的情形，故本设计聚焦双方部分重叠但非子串的中文匹配。版本降权本轮不做（用户确认）。

## 目标

- 让中文标题的模糊相似度有效，提升正确歌打分，减少"匹配错/搜不到"。
- 提高候选召回，正确版本更可能进候选池。
- 搜索前清理 query 噪声，提升召回。
- 纯逻辑改动全部 gtest 覆盖；向后兼容现有 ASCII 匹配行为与测试。

## 改动一：Matcher 中文（CJK）相似度

### 现状与保留

`jaccardSimilarity(a,b)` 内部 `tokenize` 按 ` -()，/` 切词。纯 ASCII 文本行为不变（现有测试 `JaccardSimilarity.Identical`、`HalfOverlap` 必须仍通过）。

### 新分词规则 buildMatchTokens

新增内部函数（Matcher.cpp 匿名命名空间）`std::set<std::string> buildMatchTokens(const std::string& normalized)`，替换 `jaccardSimilarity` 内的 `tokenize`：

- 逐字节扫描 `normalized`（`normalizeForMatch` 的输出：ASCII 已小写去标点、非 ASCII 原样多字节、单空格分隔）。
- ASCII 字母数字连续段 → 一个词 token（与原 tokenize 等效）。
- 空格 / 原分隔符 → 结束当前 ASCII 词。
- 非 ASCII 字节（UTF-8 多字节字符，`(c & 0x80)`）→ 先结束当前 ASCII 词，按 UTF-8 边界读取完整字符，累积到"当前 CJK 连续段"字符列表。
- 一段 CJK 字符结束（遇 ASCII 或串尾）时：对该段生成**相邻双字（bigram）** token（`chars[i]+chars[i+1]`）；若该段仅 1 个字符，用该单字作 token。

UTF-8 字符长度按首字节判定：`< 0x80` 单字节；`0xC0-0xDF` 2 字节；`0xE0-0xEF` 3 字节（覆盖 CJK 统一表意 U+4E00–U+9FFF 及中日韩标点）；`0xF0-0xF7` 4 字节。越界时按剩余字节数收尾，不读越界。

`jaccardSimilarity` 改为对 a、b 各调 `buildMatchTokens`，交集/并集比值不变。

### 效果示例

- `"晴天"` vs `"晴天"`：token {晴天} 相同 → 1.0（且 scoreText 前置 `na==nb` 已给 100）。
- `"第一天"` vs `"每一天"`：{第一,一天} vs {每一,一天}，交集 {一天} → 1/3 ≈ 0.33，进入 `scoreTitle` 第三档（阈值 0.25）得 30 分，配合 artist/duration 可越过 `kLowThreshold`。
- `"hello world"` vs `"hello world"`：纯 ASCII，{hello,world} → 1.0（行为不变）。
- `"a b"` vs `"b c"`：{a,b} vs {b,c} → 1/3（行为不变）。

### 阈值

`scoreTitle` 的 Jaccard 阈值 `{0.75,0.5,0.25}` 本轮不改：中文 bigram 相似度整体偏低，第三档 0.25→30 分即可把"部分重叠"的正确歌从 0 分抬到非零，靠 artist(0.25)+duration(0.2) 权重越过阈值。若实测召回仍不足，后续单独调阈值，不在本轮。

## 改动二：提高候选召回

- NetEaseProvider：搜索 JSON `"limit":5`→`"limit":10`，`extractSongs(searchResp, out, 5)`→`10`；title-only fallback 触发条件 `out.size() < 3` 保持；最终 `out` 超过 10 条时截断到前 10。
- QQMusicProvider：`&n=5`→`&n=10`，`extractSongList(..., 5)`→`10`，同样最终截断到前 10。
- LrcLibProvider 无 limit 参数（按 artist/title/duration 精确查询），不改。

候选变多后由 `SearchCoordinator::collectAndScore` 统一打分排序，`resolve` 仍取最高分且 `>= kLowThreshold`，逻辑不变。

## 改动三：query 归一化

新增公开函数（Matcher.h 声明、Matcher.cpp 实现）`std::string normalizeQuery(const std::string& title)`：

- 移除成对括号及其内容：`()`、`（）`、`[]`、`【】`（含中文全角括号，按 UTF-8 匹配 `（`=`\xEF\xBC\x88`、`）`=`\xEF\xBC\x89`、`【`=`\xE3\x80\x90`、`】`=`\xE3\x80\x91`）。
- 移除 `feat.` / `ft.` / `featuring` 及其后内容（借用 `normalizeForMatch` 已有的协作标记识别思路，但此处直接截断丢弃，不保留艺术家）。
- 折叠多余空白、trim。
- 若清理后为空（整标题都在括号内），回退返回原始 title，避免搜空串。

两个 Provider 的 `search` 用 `normalizeQuery(track.title)` 参与构造主 query：`fullQuery = artist.empty() ? nq : artist + " " + nq`（`nq = normalizeQuery(track.title)`）。title-only fallback 也用 `nq`。原始 `track.title` 仍用于 `Matcher` 打分（打分归一化独立，不受 query 清理影响）。

## 测试

- `tests/test_matcher.cpp` 增：CJK bigram 场景（`第一天`/`每一天` 有交集、`晴天`/`稻香` 交集 0、中英混合 `陈奕迅 eason`）、纯 ASCII 回归（保持 `Identical`/`HalfOverlap`）、`normalizeQuery`（去圆括号/中文括号/feat、全括号回退原串、纯标题不变）。
- `tests/test_netease_provider.cpp`、`tests/test_qqmusic_provider.cpp`：两者已有 `FakeHttp`（记录 `lastUrl`/`lastBody`）与 `FakeCrypto`。补两类用例：(a) 注入含 10 条歌曲的 `searchResp`，断言 `out` 解析出全部 10 条（验证 limit 提升）；(b) 用带括号/feat 的 `track.title` 调 `search`，断言 `FakeHttp.lastBody`（NetEase 的 `s` 字段）/`lastUrl`（QQ 的 `w` 参数）含归一化后的干净 query、不含被移除的括号内容。
- 全套 `core_tests` 保持全绿。

## 边界与非改动

- 不改 `Matcher` 权重、`kHighThreshold`/`kLowThreshold`、`scoreDuration`/`scoreAlbum` 逻辑。
- 不改 `SearchCoordinator` 选择流程与 `resolve` 阈值。
- 不做版本降权、不合并翻译歌词、不改歌词解析质量（用户本轮未要求）。
- CJK 判定采用"非 ASCII 即按字符 bigram"的宽松策略，日文假名、韩文亦按字符 bigram 处理，不单独区分语言。
