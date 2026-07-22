#pragma once
#include <string>
#include <utility>
#include <vector>

namespace openlyrics {

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

    // POST 请求，body 为 URL-encoded form 串（如 "params=...&encSecKey=..."）。
    // contentType 默认 application/x-www-form-urlencoded。
    virtual HttpResponse post(
        const std::string& url,
        const std::string& body,
        const std::vector<std::pair<std::string, std::string>>& headers = {}) = 0;
};

}  // namespace openlyrics
