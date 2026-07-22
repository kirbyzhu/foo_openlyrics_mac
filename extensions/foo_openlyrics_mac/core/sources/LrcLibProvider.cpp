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

}  // namespace openlyrics
