#include "LrcLibProvider.h"
#include "net/UrlEncode.h"
#include "net/JsonField.h"
#include "parser/LrcParser.h"


namespace openlyrics {

LrcLibProvider::LrcLibProvider(HttpClient& http) : http_(http) {}

bool LrcLibProvider::fetch(const TrackMeta& track, LyricData& out) {
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

    HttpResponse r = http_.get(url, {});
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

bool LrcLibProvider::search(const std::string& query, std::vector<SearchResult>& out) {
    if (query.empty()) return false;

    std::string url = "https://lrclib.net/api/search?q=" + urlEncodeComponent(query);
    HttpResponse r = http_.get(url, {});
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

bool LrcLibProvider::fetchById(int id, LyricData& out) {
    if (id <= 0) return false;

    std::string url = "https://lrclib.net/api/get?id=" + std::to_string(id);
    HttpResponse r = http_.get(url, {});
    if (r.status != 200) return false;

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

bool LrcLibProvider::search(const TrackMeta& track, std::vector<SearchResult>& out) {
    if (track.title.empty()) return false;
    std::string query = track.artist.empty() ? track.title
                                             : track.artist + " " + track.title;
    std::vector<SearchResult> raw;
    if (!search(query, raw)) return false;
    for (auto& r : raw) {
        r.source = SourceId::LrcLib;
        out.push_back(std::move(r));
    }
    return !out.empty();
}

bool LrcLibProvider::fetchById(const std::string& id, LyricData& out) {
    int intId = 0;
    try { intId = std::stoi(id); } catch (...) { return false; }
    return fetchById(intId, out);
}

}  // namespace openlyrics
