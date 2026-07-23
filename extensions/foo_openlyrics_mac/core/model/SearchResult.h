#pragma once
#include <cstdint>
#include <string>

namespace openlyrics {

enum class SourceId { Unknown, Tag, Local, LrcLib, NetEase, QQMusic };

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

struct SearchResult {
    std::string id;                    // 新增：fetchById 用的标识符
    std::string trackName;
    std::string artistName;
    std::string albumName;
    int durationSec = 0;
    SourceId source = SourceId::Unknown;  // 新增
    int score = 0;                       // 新增：Matcher 评分 0-100
};

}  // namespace openlyrics
