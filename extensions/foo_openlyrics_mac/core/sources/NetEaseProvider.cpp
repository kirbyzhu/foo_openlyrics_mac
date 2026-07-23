#include "NetEaseProvider.h"
#include "net/Base64.h"
#include "net/JsonField.h"
#include "net/UrlEncode.h"
#include "parser/LrcParser.h"

#include <random>

namespace openlyrics {

namespace {

const char* const kPresetKey = "0CoJUm6Qyw8W8jud";         // 16 字节
const char* const kIv = "0102030405060708";                 // 16 字节 ASCII
const char* const kModulusHex =
    "00e0b509f6259df8642dbc35662901477df22677ec152b5ff68ace615bb7b7251"
    "52b3ab17a876aea8a5aa76d2e417629ec4ee341f56135fccf695280104e0312ec"
    "bda92557c93870114af6c9d05c4f7f0c3685b7a46bee255932575cce10b424d8"
    "13cfe4875d3e82047b97ddef52741d546b8e289dc6935b3ece0462db0a22b8e7";
const char* const kExponentHex = "010001";
const char* const kBase62 =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

const char* const kSearchUrl = "https://music.163.com/weapi/search/get";
const char* const kLyricUrl = "https://music.163.com/weapi/song/lyric";

// 生成 16 字符随机串（base62）。thread_local 避免多线程竞态。
std::string randomKey16() {
    static std::random_device rd;
    thread_local std::mt19937 gen(rd());
    static std::uniform_int_distribution<size_t> dist(0, 61);
    std::string key;
    key.reserve(16);
    for (int i = 0; i < 16; ++i) {
        key.push_back(kBase62[dist(gen)]);
    }
    return key;
}

// 反转字符串。
std::string reverse(const std::string& s) {
    return std::string(s.rbegin(), s.rend());
}

// 在 JSON 中定位 "songs" 数组内第一个对象的 "id" 整数值。
// 用法：解析 NetEase 搜索响应的 result.songs[0].id。
bool extractFirstSongId(const std::string& json, int64_t& id) {
    // 找到 "songs" 键
    size_t pos = json.find("\"songs\"");
    if (pos == std::string::npos) return false;
    pos += 7;  // 跳过 "songs"
    // 跳过空白与冒号
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == '\r' ||
                                  json[pos] == ':'))
        ++pos;
    if (pos >= json.size() || json[pos] != '[') return false;
    ++pos;  // 跳过 [
    // 找到第一个 {
    while (pos < json.size() && json[pos] != '{') ++pos;
    if (pos >= json.size()) return false;
    std::string songObj;
    if (!jsonExtractObject(json, pos, songObj)) return false;
    return jsonGetInt(songObj, "id", id);
}

}  // namespace

NetEaseProvider::NetEaseProvider(HttpClient& http, CryptoPort& crypto)
    : http_(http), crypto_(crypto) {}

NetEaseProvider::WeapiResult NetEaseProvider::weapiEncrypt(const std::string& json) {
    WeapiResult result;  // 默认空 → 调用方判空可探错

    std::string presetKey(kPresetKey, 16);
    std::string iv(kIv, 16);

    // 第一层 AES-128-CBC：用 presetKey 加密 JSON。
    std::string step1 = crypto_.aes128CbcEncrypt(json, presetKey, iv);
    if (step1.empty()) return result;

    // 第二层 AES-128-CBC：用随机 key 加密 step1 结果。
    std::string secretKey = randomKey16();
    std::string step2 = crypto_.aes128CbcEncrypt(step1, secretKey, iv);
    if (step2.empty()) return result;

    result.params = base64Encode(step2);

    // RSA 裸加密：secretKey 反转后加密，返回 256 字符 hex。
    std::string modulusHex(kModulusHex);
    std::string exponentHex(kExponentHex);
    result.encSecKey = crypto_.rsaRawEncrypt(reverse(secretKey), modulusHex, exponentHex);

    return result;
}

std::string NetEaseProvider::weapiPost(const std::string& url, const std::string& json) {
    WeapiResult w = weapiEncrypt(json);
    if (w.params.empty() || w.encSecKey.empty()) return {};

    // 组装 URL-encoded form body。
    std::string body = "params=" + urlEncodeComponent(w.params) +
                       "&encSecKey=" + urlEncodeComponent(w.encSecKey);

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Referer", "https://music.163.com/"},
        {"Content-Type", "application/x-www-form-urlencoded"},
    };

    HttpResponse r = http_.post(url, body, headers);
    if (r.status != 200) return {};
    return r.body;
}

bool NetEaseProvider::fetch(const TrackMeta& track, LyricData& out) {
    if (track.title.empty()) return false;

    // 1. 搜索歌曲。
    std::string searchJson =
        "{\"s\":\"" + track.artist + " " + track.title +
        "\",\"type\":1,\"offset\":0,\"limit\":5}";
    std::string searchResp = weapiPost(kSearchUrl, searchJson);
    if (searchResp.empty()) return false;

    int64_t code = 0;
    if (!jsonGetInt(searchResp, "code", code) || code != 200) return false;

    int64_t songId = 0;
    if (!extractFirstSongId(searchResp, songId) || songId == 0) return false;

    // 2. 取歌词。
    std::string lyricJson =
        "{\"id\":\"" + std::to_string(songId) +
        "\",\"lv\":-1,\"tv\":-1,\"cs\":-1}";
    std::string lyricResp = weapiPost(kLyricUrl, lyricJson);
    if (lyricResp.empty()) return false;

    if (!jsonGetInt(lyricResp, "code", code) || code != 200) return false;

    // 检查纯音乐标记。
    bool noLyric = false;
    if (jsonGetBool(lyricResp, "nolyric", noLyric) && noLyric) return false;

    // 取 lrc.lyric（嵌套对象）。
    std::string lrcObj;
    if (!jsonGetObject(lyricResp, "lrc", lrcObj)) return false;

    std::string lrcText;
    if (!jsonGetString(lrcObj, "lyric", lrcText) || lrcText.empty()) return false;

    out = LrcParser::parse(lrcText);
    return true;
}

}  // namespace openlyrics
