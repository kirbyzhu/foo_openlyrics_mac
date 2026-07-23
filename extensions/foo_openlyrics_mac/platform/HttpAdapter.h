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

    // 诊断：最近一次请求的 URL/状态码/响应体长度/前缀，供调用方日志输出
    std::string lastUrl;
    int lastStatus = 0;
    size_t lastBodyLen = 0;
    std::string lastBodyPrefix;  // 响应体前 200 字符
};

}  // namespace openlyrics
