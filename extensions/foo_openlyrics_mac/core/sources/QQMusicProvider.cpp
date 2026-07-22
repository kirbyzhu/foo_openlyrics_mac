#include "QQMusicProvider.h"
#include "net/Base64.h"
#include "net/JsonField.h"
#include "net/UrlEncode.h"
#include "parser/LrcParser.h"

namespace openlyrics {

namespace {

const char* const kSearchUrl =
    "https://c.y.qq.com/soso/fcgi-bin/client_search_cp";
const char* const kLyricUrl =
    "https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg";

// 在 JSON 中找到 "list" 数组的第一个对象，提取其 "songmid" 字符串。
// 搜索响应结构：data.song.list[0].songmid。
bool extractFirstSongMid(const std::string& json, std::string& mid) {
    // 找到 "list" 键。
    size_t pos = json.find("\"list\"");
    if (pos == std::string::npos) return false;
    pos += 6;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == '\r' ||
                                  json[pos] == ':'))
        ++pos;
    if (pos >= json.size() || json[pos] != '[') return false;
    ++pos;  // 跳过 [
    // 找第一个 {
    while (pos < json.size() && json[pos] != '{') ++pos;
    if (pos >= json.size()) return false;
    // 花括号计数解析完整对象。
    size_t objStart = pos;
    int depth = 1;
    ++pos;
    while (pos < json.size() && depth > 0) {
        if (json[pos] == '{') ++depth;
        else if (json[pos] == '}') --depth;
        ++pos;
    }
    if (depth != 0) return false;
    std::string songObj = json.substr(objStart, pos - objStart);
    return jsonGetString(songObj, "songmid", mid);
}

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

bool QQMusicProvider::fetch(const TrackMeta& track, LyricData& out) {
    if (track.title.empty()) return false;

    // 1. 搜索歌曲。
    std::string searchUrl = std::string(kSearchUrl) +
                            "?w=" + urlEncodeComponent(track.artist + " " + track.title) +
                            "&p=1&n=5&format=json";

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Referer", "https://y.qq.com"},
    };

    HttpResponse searchResp = http_.get(searchUrl, headers);
    if (searchResp.status != 200) return false;

    std::string songMid;
    if (!extractFirstSongMid(searchResp.body, songMid) || songMid.empty()) {
        return false;
    }

    // 2. 取歌词。
    std::string lyricUrl = std::string(kLyricUrl) +
                           "?songmid=" + urlEncodeComponent(songMid) +
                           "&format=json&g_tk=5381";

    HttpResponse lyricResp = http_.get(lyricUrl, headers);
    if (lyricResp.status != 200) return false;

    std::string lrcText;
    if (!extractLyricText(lyricResp.body, lrcText)) return false;

    out = LrcParser::parse(lrcText);
    return true;
}

}  // namespace openlyrics
