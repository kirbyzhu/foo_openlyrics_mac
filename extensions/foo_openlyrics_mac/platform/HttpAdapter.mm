// HttpAdapter.mm
// foo_openlyrics_mac —— Plan 3 Task 6：HttpAdapter 实现，NSURLSession 同步 GET。
//
// 同步语义：NSURLSession dataTaskWithRequest:completionHandler: 本身是异步 API，用
// dispatch_semaphore 阻塞等待其完成，把返回值"翻译"成同步的 HttpClient::get 语义
// （核心层 LrcLibProvider 按同步接口编写，见 core/ports/HttpClient.h）。因此本适配器
// 绝不能在主线程调用——会阻塞主线程直到网络请求完成或超时（约 10s），造成 foobar2000
// UI 卡死。调用方保证：LyricPanelController.mm 把在线检索整体放进后台并发队列执行，
// 与 Plan 2 里 SearchPipeline::resolve() 的线程收敛方式一致。
//
// 错误处理：URL 非法、传输层错误（DNS/连接/证书等）、超时，统一表现为 status=0、
// body=""，与 core/ports/HttpClient.h 里 HttpResponse::status 的注释("0 表示传输失败")
// 对应；HTTP 层面的非 2xx 响应（如 404）仍然算"传输成功"，status 照实回填，由调用方
// （LrcLibProvider）按 status != 200 判定未命中。
#import "HttpAdapter.h"
#import "stdafx.h"

#include <strings.h>  // strcasecmp

namespace openlyrics {

namespace {

NSString* const kDefaultUserAgent = @"foo_openlyrics_mac/0.1.0 (+https://github.com)";
static int g_timeoutSec = 10;

bool HasUserAgentHeader(const std::vector<std::pair<std::string, std::string>>& headers) {
    for (const auto& kv : headers) {
        if (strcasecmp(kv.first.c_str(), "User-Agent") == 0) return true;
    }
    return false;
}

bool WaitWithCancel(dispatch_semaphore_t sem, NSURLSessionDataTask* task, NSURLSession* session, CancelToken* cancel, int timeoutSec) {
    uint64_t maxMs = static_cast<uint64_t>(timeoutSec + 1) * 1000;
    uint64_t elapsedMs = 0;
    const uint64_t stepMs = 50;
    while (elapsedMs < maxMs) {
        if (cancel && cancel->isCancelled()) {
            [task cancel];
            [session finishTasksAndInvalidate];
            return false;
        }
        dispatch_time_t tick = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(stepMs * NSEC_PER_MSEC));
        if (dispatch_semaphore_wait(sem, tick) == 0) {
            return true;
        }
        elapsedMs += stepMs;
    }
    [task cancel];
    [session finishTasksAndInvalidate];
    return false;
}

}  // namespace

HttpResponse HttpAdapter::get(const std::string& url,
                               const std::vector<std::pair<std::string, std::string>>& headers,
                               CancelToken* cancel) {
    NSCAssert(![NSThread isMainThread], @"HttpAdapter::get 不可在主线程调用（会阻塞 UI）");

    HttpResponse response;

    if (cancel && cancel->isCancelled()) return response;

    @autoreleasepool {
        NSURL* nsUrl = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (nsUrl == nil) {
            return response;
        }

        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:nsUrl];
        request.HTTPMethod = @"GET";
        request.timeoutInterval = g_timeoutSec;

        for (const auto& kv : headers) {
            [request setValue:[NSString stringWithUTF8String:kv.second.c_str()]
            forHTTPHeaderField:[NSString stringWithUTF8String:kv.first.c_str()]];
        }
        if (!HasUserAgentHeader(headers)) {
            [request setValue:kDefaultUserAgent forHTTPHeaderField:@"User-Agent"];
        }

        NSURLSessionConfiguration* config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.timeoutIntervalForRequest = g_timeoutSec;
        NSURLSession* session = [NSURLSession sessionWithConfiguration:config];

        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block NSInteger statusCode = 0;
        __block NSData* bodyData = nil;

        NSURLSessionDataTask* task = [session
            dataTaskWithRequest:request
              completionHandler:^(NSData* data, NSURLResponse* urlResponse, NSError* error) {
                  if (error == nil && [urlResponse isKindOfClass:[NSHTTPURLResponse class]]) {
                      statusCode = ((NSHTTPURLResponse*)urlResponse).statusCode;
                      bodyData = data;
                  }
                  dispatch_semaphore_signal(sem);
              }];
        [task resume];

        if (!WaitWithCancel(sem, task, session, cancel, g_timeoutSec)) {
            return response;
        }

        if (cancel && cancel->isCancelled()) {
            [session finishTasksAndInvalidate];
            return response;
        }

        [session finishTasksAndInvalidate];

        response.status = static_cast<int>(statusCode);
        if (bodyData != nil) {
            response.body = std::string(reinterpret_cast<const char*>(bodyData.bytes), bodyData.length);
        }
    }

    return response;
}

HttpResponse HttpAdapter::post(const std::string& url,
                                const std::string& body,
                                const std::vector<std::pair<std::string, std::string>>& headers,
                                CancelToken* cancel) {
    NSCAssert(![NSThread isMainThread], @"HttpAdapter::post 不可在主线程调用（会阻塞 UI）");

    HttpResponse response;

    if (cancel && cancel->isCancelled()) return response;

    @autoreleasepool {
        NSURL* nsUrl = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (nsUrl == nil) {
            return response;
        }

        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:nsUrl];
        request.HTTPMethod = @"POST";
        request.timeoutInterval = g_timeoutSec;
        request.HTTPBody = [NSData dataWithBytes:body.c_str() length:body.size()];

        bool hasContentType = false;
        for (const auto& kv : headers) {
            [request setValue:[NSString stringWithUTF8String:kv.second.c_str()]
            forHTTPHeaderField:[NSString stringWithUTF8String:kv.first.c_str()]];
            if (strcasecmp(kv.first.c_str(), "Content-Type") == 0) {
                hasContentType = true;
            }
        }
        if (!HasUserAgentHeader(headers)) {
            [request setValue:kDefaultUserAgent forHTTPHeaderField:@"User-Agent"];
        }
        if (!hasContentType) {
            [request setValue:@"application/x-www-form-urlencoded" forHTTPHeaderField:@"Content-Type"];
        }

        NSURLSessionConfiguration* config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.timeoutIntervalForRequest = g_timeoutSec;
        NSURLSession* session = [NSURLSession sessionWithConfiguration:config];

        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block NSInteger statusCode = 0;
        __block NSData* bodyData = nil;

        NSURLSessionDataTask* task = [session
            dataTaskWithRequest:request
              completionHandler:^(NSData* data, NSURLResponse* urlResponse, NSError* error) {
                  if (error == nil && [urlResponse isKindOfClass:[NSHTTPURLResponse class]]) {
                      statusCode = ((NSHTTPURLResponse*)urlResponse).statusCode;
                      bodyData = data;
                  }
                  dispatch_semaphore_signal(sem);
              }];
        [task resume];

        if (!WaitWithCancel(sem, task, session, cancel, g_timeoutSec)) {
            return response;
        }

        if (cancel && cancel->isCancelled()) {
            [session finishTasksAndInvalidate];
            return response;
        }

        [session finishTasksAndInvalidate];

        response.status = static_cast<int>(statusCode);
        if (bodyData != nil) {
            response.body = std::string(reinterpret_cast<const char*>(bodyData.bytes), bodyData.length);
        }
    }

    return response;
}

int HttpAdapter::globalTimeout() { return g_timeoutSec; }
void HttpAdapter::setGlobalTimeout(int seconds) {
    if (seconds >= 1) g_timeoutSec = seconds;
}

}  // namespace openlyrics
