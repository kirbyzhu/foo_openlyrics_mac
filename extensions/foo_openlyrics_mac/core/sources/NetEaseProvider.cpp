#include "NetEaseProvider.h"
#include "net/JsonField.h"
#include "net/UrlEncode.h"
#include "parser/LrcParser.h"

#include <set>
#include <algorithm>

namespace openlyrics {

namespace {

const char* const kEapiHost = "https://interface.music.163.com";
const char* const kSearchPath = "/eapi/cloudsearch/pc";
const char* const kLyricPath = "/eapi/song/lyric/v1";
const char* const kEapiKey = "e82ckenh8dichen8";  // 16 字节 AES-ECB key

// 大写 hex 编码
std::string hexEncodeUpper(const std::string& data) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 2);
    for (unsigned char c : data) {
        out.push_back(kHex[c >> 4]);
        out.push_back(kHex[c & 0x0F]);
    }
    return out;
}

// 从 JSON 数组中提取多条歌曲对象。pos 初始指向 '['，结束时指向 ']' 之后。
// 使用宽松解析：跳过任意字符直到遇到 '{' 或 ']'，兼容不同服务端 JSON 格式。
bool extractSongsArray(const std::string& json, size_t& pos,
                       std::vector<std::string>& out, int limit) {
    // pos 应指向 '[' 之后的首个字符
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == '\r' || json[pos] == ':'))
        ++pos;
    if (pos >= json.size() || json[pos] != '[') return false;
    ++pos;  // 跳过 [
    for (int i = 0; i < limit; ++i) {
        // 宽松跳过：与旧 extractFirstSongId 保持一致，跳过任意非 '{' 非 ']' 字符
        while (pos < json.size() && json[pos] != '{' && json[pos] != ']') ++pos;
        if (pos >= json.size() || json[pos] == ']') break;
        if (json[pos] != '{') return false;
        std::string obj;
        if (!jsonExtractObject(json, pos, obj)) break;
        out.push_back(std::move(obj));
    }
    return true;
}

}  // namespace

NetEaseProvider::NetEaseProvider(HttpClient& http, CryptoPort& crypto)
    : http_(http), crypto_(crypto) {}

std::string NetEaseProvider::eapiEncrypt(const std::string& path, const std::string& params) {
    // 加密用的 path 需将 "eapi" 替换为 "api"
    std::string apiPath = path;
    size_t pos = apiPath.find("eapi");
    if (pos != std::string::npos) {
        apiPath.replace(pos, 4, "api");
    }

    // sign = MD5("nobody" + api_path + "use" + params + "md5forencrypt")
    std::string signInput = std::string("nobody") + apiPath +
                            "use" + params + "md5forencrypt";
    std::string sign = crypto_.md5Hex(signInput);

    // source = api_path + "-36cd479b6b5-" + params + "-36cd479b6b5-" + sign
    std::string source = apiPath + "-36cd479b6b5-" +
                         params + "-36cd479b6b5-" + sign;

    // AES-128-ECB encrypt with key "e82ckenh8dichen8"
    std::string key(kEapiKey, 16);
    std::string encrypted = crypto_.aes128EcbEncrypt(source, key);
    if (encrypted.empty()) return {};

    return hexEncodeUpper(encrypted);
}

std::string NetEaseProvider::eapiPost(const std::string& urlPath, const std::string& json) {
    std::string params = eapiEncrypt(urlPath, json);
    if (params.empty()) return {};

    std::string body = "params=" + urlEncodeComponent(params);
    std::string fullUrl = std::string(kEapiHost) + urlPath;

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Cookie", "os=pc; appver=3.1.3.203419;"},
        {"Origin", "orpheus://orpheus"},
        {"User-Agent",
         "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
         "AppleWebKit/537.36 (KHTML, like Gecko) "
         "NeteaseMusicDesktop/3.1.3.203419"},
        {"Content-Type", "application/x-www-form-urlencoded"},
    };

    HttpResponse r = http_.post(fullUrl, body, headers);
    if (r.status != 200) return {};
    return r.body;
}

SearchResult NetEaseProvider::parseSongObject(const std::string& objJson) {
    SearchResult sr;
    sr.source = SourceId::NetEase;

    int64_t idVal = 0;
    if (jsonGetInt(objJson, "id", idVal)) sr.id = std::to_string(idVal);
    jsonGetString(objJson, "name", sr.trackName);

    // ar 是数组：ar[0].name。jsonGetObject 不支持提取数组值，
    // 改为手动搜索 "ar" 键并提取数组的第一个对象。
    size_t arKeyPos = objJson.find("\"ar\"");
    if (arKeyPos != std::string::npos) {
        arKeyPos += 4; // 跳过 "ar"
        while (arKeyPos < objJson.size() && (objJson[arKeyPos] == ' ' || objJson[arKeyPos] == '\t' ||
                                              objJson[arKeyPos] == '\n' || objJson[arKeyPos] == '\r' ||
                                              objJson[arKeyPos] == ':'))
            ++arKeyPos;
        if (arKeyPos < objJson.size() && objJson[arKeyPos] == '[') {
            ++arKeyPos; // 跳过 [
            while (arKeyPos < objJson.size() && (objJson[arKeyPos] == ' ' || objJson[arKeyPos] == '\t' ||
                                                  objJson[arKeyPos] == '\n' || objJson[arKeyPos] == '\r'))
                ++arKeyPos;
            std::string firstAr;
            if (jsonExtractObject(objJson, arKeyPos, firstAr)) {
                jsonGetString(firstAr, "name", sr.artistName);
            }
        }
    }

    // al 是对象：al.name
    std::string alObj;
    if (jsonGetObject(objJson, "al", alObj)) {
        jsonGetString(alObj, "name", sr.albumName);
    }

    // dt 是时长（毫秒）
    int64_t dtMs = 0;
    if (jsonGetInt(objJson, "dt", dtMs) && dtMs > 0) {
        sr.durationSec = static_cast<int>(dtMs / 1000);
    }

    return sr;
}

bool NetEaseProvider::extractSongs(const std::string& json,
                                    std::vector<SearchResult>& out, int limit) {
    size_t pos = json.find("\"songs\"");
    if (pos == std::string::npos) return false;
    pos += 7;
    std::vector<std::string> songObjects;
    if (!extractSongsArray(json, pos, songObjects, limit)) return false;
    for (const auto& obj : songObjects) {
        out.push_back(parseSongObject(obj));
    }
    return !out.empty();
}

bool NetEaseProvider::search(const TrackMeta& track, std::vector<SearchResult>& out) {
    if (track.title.empty()) return false;

    auto tryQuery = [&](const std::string& query) {
        // cloudsearch/pc 返回明文 {"result":{"songs":[...]}}，不带 e_r 避免加密响应
        std::string searchJson =
            "{\"s\":\"" + jsonEscapeString(query) +
            "\",\"type\":1,\"limit\":5,\"offset\":0,\"total\":true}";
        std::string searchResp = eapiPost(kSearchPath, searchJson);
        if (searchResp.empty()) return false;

        int64_t code = 0;
        if (!jsonGetInt(searchResp, "code", code) || code != 200) return false;

        return extractSongs(searchResp, out, 5);
    };

    std::string fullQuery = track.artist.empty() ? track.title
                                                  : track.artist + " " + track.title;
    bool ok = tryQuery(fullQuery);

    // 若 artist+title 搜索结果 < 3 条且 artist 非空，追加 title-only 搜索
    if (out.size() < 3 && !track.artist.empty()) {
        std::set<std::string> seen;
        for (const auto& r : out) seen.insert(r.id);
        size_t before = out.size();
        tryQuery(track.title);
        // 去重：移除 title-only 搜索中已存在的 id
        out.erase(std::remove_if(out.begin() + before, out.end(),
                    [&](const SearchResult& r) { return seen.count(r.id); }),
                  out.end());
    }

    return ok || !out.empty();
}

bool NetEaseProvider::fetchById(const std::string& id, LyricData& out) {
    if (id.empty()) return false;

    std::string lyricJson =
        "{\"id\":\"" + jsonEscapeString(id) +
        "\",\"lv\":\"-1\",\"tv\":\"-1\",\"rv\":\"-1\",\"yv\":\"-1\"}";
    std::string lyricResp = eapiPost(kLyricPath, lyricJson);
    if (lyricResp.empty()) return false;

    int64_t code = 0;
    if (!jsonGetInt(lyricResp, "code", code) || code != 200) return false;

    bool noLyric = false;
    if (jsonGetBool(lyricResp, "nolyric", noLyric) && noLyric) return false;

    std::string lrcObj;
    if (!jsonGetObject(lyricResp, "lrc", lrcObj)) return false;

    std::string lrcText;
    if (!jsonGetString(lrcObj, "lyric", lrcText) || lrcText.empty()) return false;

    out = LrcParser::parse(lrcText);
    return true;
}

}  // namespace openlyrics
