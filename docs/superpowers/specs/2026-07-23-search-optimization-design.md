# 歌词搜索优化设计

最后更新 2026-07-23。参考 LDDC (github.com/chenmozhijin/LDDC) 的搜索设计，对 foo_openlyrics_mac 的歌词搜索功能进行优化完善。

## 一、目标与范围

两个方向同时推进：

- **A 方向**：补齐 NetEase/QQMusic 的手动搜索能力，手动搜索面板按源分组展示三平台候选列表，用户选择最匹配结果。
- **B 方向**：引入多候选评分机制（标题+艺术家+时长+专辑），提升自动取词命中率。

核心设计原则：最小改动，最大效果。不改动的文件：`SearchPipeline`、`TagSource`、`LocalFileSource`、`LrcLibProvider`（已有 search/fetchById）、`LrcParser`、`LyricStore`、`SyncEngine`。

## 二、统一搜索路径

两个模式走同一条路径：收集候选池 → 评分 → 按模式分发结果。

```
LyricPanelController
       │
┌──────▼──────────────────────────────────┐
│         SearchCoordinator               │
│                                         │
│  ┌─────────────────────────────────┐    │
│  │ 1. 本地快速通道                  │    │
│  │    SearchPipeline (Tag→Local)   │    │
│  │    命中 → 直接返回               │    │
│  └───────────┬─────────────────────┘    │
│              │ 未命中                    │
│  ┌───────────▼─────────────────────┐    │
│  │ 2. 并行调用所有在线源 search()    │    │
│  │    产出统一候选池 (带 sourceId)   │    │
│  └───────────┬─────────────────────┘    │
│              │                          │
│  ┌───────────▼─────────────────────┐    │
│  │ 3. Matcher 对每条候选评分        │    │
│  │    候选池按分数降序             │    │
│  └───────────┬─────────────────────┘    │
│              │                          │
│     ┌────────▼────────┐                 │
│     │  mode?           │                 │
│     └──┬───────────┬──┘                 │
│  auto │           │ manual               │
│  ┌────▼────┐  ┌───▼──────────────┐      │
│  │ 取最高分  │  │ 按 sourceId      │      │
│  │ ≥阈值?   │  │ 分组返回全部候选   │      │
│  │ →fetchById│  │ (含分数,供UI排序) │      │
│  └─────────┘  └──────────────────┘      │
└─────────────────────────────────────────┘
```

## 三、LyricSource 接口扩展

```cpp
class LyricSource {
public:
    virtual ~LyricSource() = default;
    virtual bool search(const TrackMeta& track, std::vector<SearchResult>& out);
    virtual bool fetchById(const std::string& id, LyricData& out);
    virtual bool fetch(const TrackMeta& track, LyricData& out);
};
```

`search()` 和 `fetchById()` 有默认实现返回空/ false，子类按能力覆写。`fetch()` 默认实现：search 取第一候选 → fetchById。`TagSource` 和 `LocalFileSource` 覆写 `fetch()` 保持现有逻辑，不覆写 `search()`/`fetchById()`。

各 Provider 实现矩阵：

| Provider | search() | fetchById() | fetch() 行为 |
|---|---|---|---|
| TagSource | 不覆写 | 不覆写 | 保持现有：读内嵌标签 |
| LocalFileSource | 不覆写 | 不覆写 | 保持现有：扫描同目录文件 |
| LrcLibProvider | 已实现 | 已实现 | 默认：search→fetchById |
| NetEaseProvider | 新增 | 新增 | 默认：search→fetchById |
| QQMusicProvider | 新增 | 新增 | 默认：search→fetchById |

`fetchById` 的 `id` 参数为字符串类型：LrcLib 用数字 ID、NetEase 用 songId、QQMusic 用 songmid，字符串统一容纳。

## 四、SearchResult 扩展

```cpp
enum class SourceId { Unknown, Tag, Local, LrcLib, NetEase, QQMusic };

struct SearchResult {
    std::string id;           // 新增：fetchById 用的标识符
    std::string trackName;
    std::string artistName;
    std::string albumName;
    int durationSec = 0;
    SourceId source = SourceId::Unknown;  // 新增：来源枚举
    int score = 0;           // 新增：Matcher 评分 (0-100)
};

// UI 层提供显示名映射
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
```

## 五、Matcher 评分器

### 5.1 接口

```cpp
namespace openlyrics {

struct MatchWeights {
    float title    = 0.40f;
    float artist   = 0.25f;
    float album    = 0.15f;
    float duration = 0.20f;
};

class Matcher {
public:
    explicit Matcher(const MatchWeights& w = {});
    int score(const TrackMeta& track, const SearchResult& candidate) const;
    bool isHighConfidence(int s) const;
    bool isLowConfidence(int s) const;

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

### 5.2 标题/艺术家相似度（混合算法）

先做协作标记归一化：`feat.`/`ft.`/`featuring`/`with` 及其后内容置换为 `(feat. X)` 形式，消除 `"A feat. B"` vs `"A (feat. B)"` 的差异。

| 条件 | 得分 |
|---|---|
| 归一化后完全相等 | 100 |
| 归一化后一方包含另一方 | 90 |
| 分词 Jaccard ≥ 0.75 | 80 |
| 分词 Jaccard ≥ 0.5 | 60 |
| 分词 Jaccard ≥ 0.25 | 30 |
| 其他 | 0 |

艺术家维度阈值调整为 0.8/0.6/0.3（艺术家名更短，容错更小）。

### 5.3 时长匹配

| 条件 | 得分 |
|---|---|
| 候选有有效时长且偏差 ≤ 3 秒 | 100 |
| 偏差 ≤ 8 秒 | 70 |
| 偏差 ≤ 15 秒 | 40 |
| 候选无时长或偏差 > 15 秒 | 0 |

### 5.4 专辑匹配

归一化后子串包含 → 100，Jaccard ≥ 0.5 → 60，否则 0。

### 5.5 归一化函数

小写 + 去标点空白 + 全角转半角 + 协作标记置换。与现有 `LocalFileSource::normalize()` 共享实现，补充协作标记处理。

## 六、SearchCoordinator 编排层

```cpp
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

    bool resolve(const TrackMeta& track, LyricData& out);
    std::vector<GroupedResults> searchAll(const TrackMeta& track);

private:
    std::vector<SearchResult> collectAndScore(const TrackMeta& track);

    SearchPipeline& localPipeline_;
    std::vector<LyricSource*> onlineSources_;
    Matcher& matcher_;
    static constexpr int kAutoThreshold = 70;
};

}  // namespace openlyrics
```

### resolve() 流程

1. `localPipeline_.resolve(track, out)` — 命中直接返回
2. 未命中 → `collectAndScore(track)` — 并行 search() 所有在线源 → 评分排序
3. 最高分 ≥ 70 → fetchById → 返回
4. 最高分 ≥ 40 → fetchById → 返回（低置信度但不阻塞）
5. 否则返回 false

### searchAll() 流程

1. `collectAndScore(track)` — 并行搜索 + 评分
2. 按 sourceId 分组，每组内按分数降序
3. 返回 `std::vector<GroupedResults>`

## 七、Provider 改动

### 7.1 NetEaseProvider

将现有 `fetch()` 中的私有搜索逻辑拆分为 public 方法：

```cpp
class NetEaseProvider : public LyricSource {
public:
    NetEaseProvider(HttpClient& http, CryptoPort& crypto);

    bool search(const TrackMeta& track, std::vector<SearchResult>& out) override;
    bool fetchById(const std::string& id, LyricData& out) override;
    // fetch() 使用默认实现：search → fetchById

private:
    // 保留 weapiEncrypt / weapiPost 等现有私有方法不变
};
```

`search()` 复用现有的 weapi 搜索请求（搜索接口已调通），解析 `result.songs[]` 数组的前 N 条（建议 5 条）填入 SearchResult，id 填入 songId。`fetchById()` 用传入的 songId 调用 weapi 歌词接口。

### 7.2 QQMusicProvider

同上拆分：

```cpp
class QQMusicProvider : public LyricSource {
public:
    QQMusicProvider(HttpClient& http, CryptoPort& crypto);

    bool search(const TrackMeta& track, std::vector<SearchResult>& out) override;
    bool fetchById(const std::string& id, LyricData& out) override;

private:
    // 保留现有私有方法不变
};
```

`search()` 解析搜索响应的 `data.song.list[]` 数组前 N 条填入 SearchResult，id 填入 songmid。`fetchById()` 用传入的 songmid 调用歌词接口。

## 八、UI 改动

### 8.1 LyricPanelController

- 构造 `SearchCoordinator`，注入 `SearchPipeline` + 三个在线 Provider + `Matcher`
- 播放切换回调中调用 `coordinator.resolve()` 替代当前的 `pipeline_.resolve()`
- 手动搜索动作调用 `coordinator.searchAll()` 替代当前的 `lrclib_.search()`

### 8.2 搜索结果面板

- 按源分 section 显示：LrcLib / 网易云音乐 / QQ 音乐
- 每个 section 内按分数降序排列
- 每条结果显示：trackName、artistName、albumName、时长、匹配分数
- 选中后调用对应 Provider 的 fetchById，落盘并显示

## 九、文件改动清单

| 文件 | 改动类型 | 说明 |
|---|---|---|
| `core/sources/LyricSource.h` | 修改 | 新增 search/fetchById 虚方法 + fetch 默认实现 |
| `core/model/SearchResult.h` | 修改 | 新增 id/source/score 字段，新增 SourceId 枚举 |
| `core/model/TrackMeta.h` | 不改 | — |
| `core/model/LyricData.h` | 不改 | — |
| `core/matching/Matcher.h` | 新增 | 评分器声明 |
| `core/matching/Matcher.cpp` | 新增 | 评分器实现（含 normalize/分词/Jaccard） |
| `core/pipeline/SearchCoordinator.h` | 新增 | 编排层声明 |
| `core/pipeline/SearchCoordinator.cpp` | 新增 | 编排层实现 |
| `core/pipeline/SearchPipeline.h` | 不改 | — |
| `core/pipeline/SearchPipeline.cpp` | 不改 | — |
| `core/sources/NetEaseProvider.h` | 修改 | 新增 search/fetchById 声明 |
| `core/sources/NetEaseProvider.cpp` | 修改 | 拆分实现，新增 search/fetchById |
| `core/sources/QQMusicProvider.h` | 修改 | 新增 search/fetchById 声明 |
| `core/sources/QQMusicProvider.cpp` | 修改 | 拆分实现，新增 search/fetchById |
| `core/sources/LrcLibProvider.h` | 不改 | 已有 search/fetchById |
| `core/sources/LrcLibProvider.cpp` | 不改 | — |
| `core/sources/TagSource.h` | 不改 | 仍覆写 fetch() |
| `core/sources/TagSource.cpp` | 不改 | — |
| `core/sources/LocalFileSource.h` | 不改 | 仍覆写 fetch() |
| `core/sources/LocalFileSource.cpp` | 不改 | — |
| `ui/LyricPanelController.mm` | 修改 | 接线 SearchCoordinator，手动搜索调 searchAll |
| `CMakeLists.txt` | 修改 | 添加 matching/ 目录，新增源文件 |
| `tests/test_matcher.cpp` | 新增 | 评分器单测 |
| `tests/test_search_coordinator.cpp` | 新增 | 编排层单测 |
| `tests/test_netease_provider.cpp` | 修改 | 补充 search/fetchById 用例 |
| `tests/test_qqmusic_provider.cpp` | 修改 | 补充 search/fetchById 用例 |

## 十、测试计划

### 10.1 Matcher 单元测试

- 标题完全相等 → 标题分 100，总分验证
- 标题归一化后包含 → 90
- 标题 Jaccard 各级阈值
- 协作标记归一化："Artist feat. B" ↔ "Artist (feat. B)"
- 艺术家短名匹配
- 时长三级偏差
- 专辑子串/无专辑
- 综合评分边界：全满分、全零分、混合场景
- 高/低置信度阈值判定

### 10.2 SearchCoordinator 单元测试

- 本地快速通道命中 → 直接返回，不调在线源
- 本地未命中 → 在线候选评分取最优
- 在线最高分低于阈值 → 返回 false
- searchAll → 按源分组 + 组内降序
- 某在线源失败 → 不影响其他源

### 10.3 Provider 测试

- NetEase search 返回多条候选，id 字段非空
- NetEase fetchById 能拉到歌词
- QQMusic search 返回多条候选，id 字段非空
- QQMusic fetchById 能拉到歌词
