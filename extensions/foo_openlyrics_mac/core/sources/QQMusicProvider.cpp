#include "QQMusicProvider.h"
#include "net/Base64.h"
#include "net/JsonField.h"
#include "net/UrlEncode.h"
#include "parser/LrcParser.h"
#include "matching/Matcher.h"
#include <set>
#include <algorithm>

namespace openlyrics {

namespace {

const char* const kSearchUrl =
    "https://c.y.qq.com/soso/fcgi-bin/client_search_cp";
const char* const kLyricUrl =
    "https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg";

// 从歌词 API 响应中提取 base64 编码的 lyric 字段并解码。
bool extractLyricText(const std::string& resp, std::string& lrcText) {
    int64_t code = 0;
    if (!jsonGetInt(resp, "code", code) || code != 0) return false;
    std::string encoded;
    if (!jsonGetString(resp, "lyric", encoded) || encoded.empty()) return false;
    lrcText = base64Decode(encoded);
    return !lrcText.empty();
}

}  // namespace

QQMusicProvider::QQMusicProvider(HttpClient& http, CryptoPort& crypto)
    : http_(http), crypto_(crypto) {}

bool QQMusicProvider::search(const TrackMeta& track, std::vector<SearchResult>& out,
                             CancelToken* cancel) {
    if (track.title.empty()) return false;

    auto tryQuery = [&](const std::string& query) {
        std::string searchUrl = std::string(kSearchUrl) +
                                "?w=" + urlEncodeComponent(query) +
                                "&p=1&n=10&format=json";

        std::vector<std::pair<std::string, std::string>> headers = {
            {"Referer", "https://y.qq.com"},
        };

        HttpResponse searchResp = http_.get(searchUrl, headers, cancel);
        if (searchResp.status != 200) return false;

        int64_t code = 0;
        if (!jsonGetInt(searchResp.body, "code", code) || code != 0) return false;

        return extractSongList(searchResp.body, out, 10);
    };

    std::string nq = normalizeQuery(track.title);
    std::string fullQuery = track.artist.empty() ? nq : track.artist + " " + nq;
    bool ok = tryQuery(fullQuery);

    if (cancel && cancel->isCancelled()) return ok || !out.empty();

    // 若 artist+title 搜索结果 < 3 条且 artist 非空，追加 title-only 搜索
    if (out.size() < 3 && !track.artist.empty()) {
        std::set<std::string> seen;
        for (const auto& r : out) seen.insert(r.id);
        size_t before = out.size();
        tryQuery(nq);
        // 去重
        out.erase(std::remove_if(out.begin() + before, out.end(),
                    [&](const SearchResult& r) { return seen.count(r.id); }),
                  out.end());
    }

    if (out.size() > 10) out.resize(10);
    return ok || !out.empty();
}

bool QQMusicProvider::fetchById(const std::string& id, LyricData& out, CancelToken* cancel) {
    if (id.empty()) return false;

    std::string lyricUrl = std::string(kLyricUrl) +
                           "?songmid=" + urlEncodeComponent(id) +
                           "&format=json&g_tk=5381";

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Referer", "https://y.qq.com"},
    };

    HttpResponse lyricResp = http_.get(lyricUrl, headers, cancel);
    if (lyricResp.status != 200) return false;

    std::string lrcText;
    if (!extractLyricText(lyricResp.body, lrcText)) return false;

    out = LrcParser::parse(lrcText);
    return true;
}

bool QQMusicProvider::extractSongList(const std::string& json, std::vector<SearchResult>& out, int limit) {
    // 先定位 "song" 对象，再在其内部找 "list" 数组，避免被 semantic.list 误导。
    size_t songPos = json.find("\"song\"");
    if (songPos == std::string::npos) return false;
    songPos += 6;
    while (songPos < json.size() && (json[songPos] == ' ' || json[songPos] == '\t' ||
                                      json[songPos] == '\n' || json[songPos] == '\r' ||
                                      json[songPos] == ':'))
        ++songPos;
    std::string songObj;
    if (!jsonExtractObject(json, songPos, songObj)) return false;

    // 在 song 对象内找 "list" 键
    size_t pos = songObj.find("\"list\"");
    if (pos == std::string::npos) return false;
    pos += 6;
    while (pos < songObj.size() && (songObj[pos] == ' ' || songObj[pos] == '\t' ||
                                     songObj[pos] == '\n' || songObj[pos] == '\r' ||
                                     songObj[pos] == ':'))
        ++pos;
    while (pos < songObj.size() && (songObj[pos] == ' ' || songObj[pos] == '\t' ||
                                     songObj[pos] == '\n' || songObj[pos] == '\r' ||
                                     songObj[pos] == ':'))
        ++pos;
    if (pos >= songObj.size() || songObj[pos] != '[') return false;
    ++pos;  // 跳过 [

    for (int i = 0; i < limit; ++i) {
        // 宽松跳过：与 NetEase parse 一致，跳过任意非 '{' 非 ']' 字符
        while (pos < songObj.size() && songObj[pos] != '{' && songObj[pos] != ']') ++pos;
        if (pos >= songObj.size() || songObj[pos] == ']') break;
        if (songObj[pos] != '{') return false;

        std::string obj;
        if (!jsonExtractObject(songObj, pos, obj)) break;

        SearchResult sr;
        sr.source = SourceId::QQMusic;

        jsonGetString(obj, "songmid", sr.id);
        jsonGetString(obj, "songname", sr.trackName);

        // singer 是数组：singer[0].name
        // jsonGetObject 不支持数组，手工定位 "singer" 后跳过 [ 然后提取第一个对象
        size_t singerPos = obj.find("\"singer\"");
        if (singerPos != std::string::npos) {
            singerPos += 8;  // 跳过 "singer"
            while (singerPos < obj.size() && (obj[singerPos] == ' ' || obj[singerPos] == '\t' ||
                                              obj[singerPos] == '\n' || obj[singerPos] == '\r' ||
                                              obj[singerPos] == ':'))
                ++singerPos;
            if (singerPos < obj.size() && obj[singerPos] == '[') {
                ++singerPos;
                while (singerPos < obj.size() && (obj[singerPos] == ' ' || obj[singerPos] == '\t' ||
                                                  obj[singerPos] == '\n' || obj[singerPos] == '\r' ||
                                                  obj[singerPos] == ','))
                    ++singerPos;
                if (singerPos < obj.size() && obj[singerPos] == '{') {
                    std::string firstSinger;
                    if (jsonExtractObject(obj, singerPos, firstSinger)) {
                        jsonGetString(firstSinger, "name", sr.artistName);
                    }
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

}  // namespace openlyrics
