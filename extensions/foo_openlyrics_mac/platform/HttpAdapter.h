// HttpAdapter.h
// foo_openlyrics_mac —— Plan 3 Task 6：HttpClient 端口的 NSURLSession 实现。
//
// 头文件只暴露纯 C++ 接口（openlyrics::HttpClient 的实现），不引入 Cocoa/Foundation
// 类型，保持与 core/ports/HttpClient.h 相同的可移植性；真正触碰 NSURLSession 的代码
// 全部关在 HttpAdapter.mm 里。同步/线程约束见 HttpAdapter.mm 顶部注释。
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
};

}  // namespace openlyrics
