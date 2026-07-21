#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace openlyrics {

struct TrackMeta {
    std::string artist;
    std::string title;
    std::string album;
    std::string path;
    int64_t lengthMs = 0;
};

struct HttpResponse {
    int status = 0;      // HTTP 状态码，0 表示传输失败
    std::string body;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse get(
        const std::string& url,
        const std::vector<std::pair<std::string, std::string>>& headers = {}) = 0;
};

}  // namespace openlyrics
