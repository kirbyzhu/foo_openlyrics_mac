#pragma once
#include "ports/HttpClient.h"

namespace openlyrics {

class HttpAdapter : public HttpClient {
public:
    HttpResponse get(const std::string& url,
                      const std::vector<std::pair<std::string, std::string>>& headers) override;
    HttpResponse post(const std::string& url,
                       const std::string& body,
                       const std::vector<std::pair<std::string, std::string>>& headers) override;

    // 全局超时秒数，所有 HttpAdapter 实例共享。默认 10s。
    static void setGlobalTimeout(int seconds);
    static int globalTimeout();
};

}  // namespace openlyrics
