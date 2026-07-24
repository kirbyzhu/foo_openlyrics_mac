#include "LrcLibProvider.h"
#include "net/UrlEncode.h"
#include "net/JsonField.h"
#include "parser/LrcParser.h"
#include <set>


namespace openlyrics {

LrcLibProvider::LrcLibProvider(HttpClient& http) : http_(http) {}

bool LrcLibProvider::fetch(const TrackMeta& track, LyricData& out, CancelToken* cancel) {
    if (track.title.empty()) {
        return false;
    }

    std::string url = "https://lrclib.net/api/get?artist_name=" +
                       urlEncodeComponent(track.artist) +
                       "&track_name=" + urlEncodeComponent(track.title);
    if (!track.album.empty()) {
        url += "&album_name=" + urlEncodeComponent(track.album);
    }
    if (track.lengthMs > 0) {
        url += "&duration=" + std::to_string(track.lengthMs / 1000);
    }

    HttpResponse r = http_.get(url, {}, cancel);
    if (r.status != 200) {
        return false;
    }

    bool instrumental = false;
    if (jsonGetBool(r.body, "instrumental", instrumental) && instrumental) {
        return false;
    }

    std::string synced;
    if (jsonGetString(r.body, "syncedLyrics", synced) && !synced.empty()) {
        out = LrcParser::parse(synced);
        return true;
    }

    std::string plain;
    if (jsonGetString(r.body, "plainLyrics", plain) && !plain.empty()) {
        out = LrcParser::parse(plain);
        return true;
    }

    return false;
}

bool LrcLibProvider::search(const std::string& query, std::vector<SearchResult>& out, CancelToken* cancel) {
    if (query.empty()) return false;

    std::string url = "https://lrclib.net/api/search?q=" + urlEncodeComponent(query);
    HttpResponse r = http_.get(url, {}, cancel);
    if (r.status != 200) return false;

    // 手动解析 JSON 数组：逐元素提取 {} 块再取字段。
    const std::string& body = r.body;
    size_t pos = 0;
    // 跳过前导空白与 '['
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t' ||
                                  body[pos] == '\n' || body[pos] == '\r'))
        ++pos;
    if (pos >= body.size() || body[pos] != '[') return false;
    ++pos;

    for (;;) {
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t' ||
                                      body[pos] == '\n' || body[pos] == '\r' || body[pos] == ','))
            ++pos;
        if (pos >= body.size() || body[pos] == ']') break;

        // 找当前对象的完整 {} 块（正确处理字符串内花括号）
        if (body[pos] != '{') return false;
        std::string obj;
        if (!jsonExtractObject(body, pos, obj)) break;

        SearchResult sr;
        // id 可能为 int，用 jsonGetInt 读取
        int64_t idVal = 0;
        if (jsonGetInt(obj, "id", idVal)) sr.id = std::to_string(idVal);
        jsonGetString(obj, "trackName", sr.trackName);
        jsonGetString(obj, "artistName", sr.artistName);
        jsonGetString(obj, "albumName", sr.albumName);
        int64_t dur = 0;
        if (jsonGetInt(obj, "duration", dur)) sr.durationSec = static_cast<int>(dur);

        if (!sr.id.empty()) out.push_back(std::move(sr));
    }

    return !out.empty();
}

bool LrcLibProvider::fetchById(int id, LyricData& out, CancelToken* cancel) {
    if (id <= 0) return false;

    std::string url = "https://lrclib.net/api/get?id=" + std::to_string(id);
    HttpResponse r = http_.get(url, {}, cancel);
    if (r.status != 200) {
        // ID 端点返回非 200，此系已知现象：LrcLib /api/get?id=N 可能因 ID 失效、
        // 限流或服务端策略返回错误。调用方应回退到命名查询 fetch()。
        return false;
    }

    bool instrumental = false;
    if (jsonGetBool(r.body, "instrumental", instrumental) && instrumental) {
        return false;
    }

    std::string synced;
    if (jsonGetString(r.body, "syncedLyrics", synced) && !synced.empty()) {
        out = LrcParser::parse(synced);
        return true;
    }

    std::string plain;
    if (jsonGetString(r.body, "plainLyrics", plain) && !plain.empty()) {
        out = LrcParser::parse(plain);
        return true;
    }

    return false;
}

bool LrcLibProvider::search(const TrackMeta& track, std::vector<SearchResult>& out, CancelToken* cancel) {
    if (track.title.empty()) return false;

    // 两轮搜索：先 artist+title，未命中或结果少则追加 title-only
    auto collect = [&](const std::string& query) {
        std::vector<SearchResult> raw;
        if (search(query, raw, cancel)) {
            for (auto& r : raw) {
                r.source = SourceId::LrcLib;
                out.push_back(std::move(r));
            }
        }
    };

    std::string fullQuery = track.artist.empty() ? track.title
                                                  : track.artist + " " + track.title;
    collect(fullQuery);

    if (cancel && cancel->isCancelled()) return !out.empty();

    // 若 artist+title 搜索结果 < 3 条，且 artist 非空，追加 title-only 搜索扩大候选池
    if (out.size() < 3 && !track.artist.empty()) {
        // 按 id 去重：已收集的 id 不重复加入
        std::set<std::string> seen;
        for (const auto& r : out) seen.insert(r.id);

        std::vector<SearchResult> titleOnly;
        if (search(track.title, titleOnly, cancel)) {
            for (auto& r : titleOnly) {
                if (seen.count(r.id)) continue;
                r.source = SourceId::LrcLib;
                out.push_back(std::move(r));
            }
        }
    }

    return !out.empty();
}

bool LrcLibProvider::fetchById(const std::string& id, LyricData& out, CancelToken* cancel) {
    int intId = 0;
    try { intId = std::stoi(id); } catch (...) { return false; }
    return fetchById(intId, out, cancel);
}

}  // namespace openlyrics
