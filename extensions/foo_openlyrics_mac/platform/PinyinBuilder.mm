#import "PinyinBuilder.h"
#import <Foundation/Foundation.h>

#include <cctype>
#include <mutex>
#include <string>
#include <unordered_map>

#include "search/PinyinPolyphonic.h"

namespace openlyrics_platform {
namespace {

// 用 CFStringTransform 把单个汉字码点转小写无声调拼音；失败返回空串。
std::string transformSingleHanzi(char32_t cp) {
    UniChar buf[2];
    CFIndex len = 0;
    if (cp <= 0xFFFF) {
        buf[len++] = static_cast<UniChar>(cp);
    } else {
        char32_t v = cp - 0x10000;
        buf[len++] = static_cast<UniChar>(0xD800 + (v >> 10));
        buf[len++] = static_cast<UniChar>(0xDC00 + (v & 0x3FF));
    }
    CFMutableStringRef s = CFStringCreateMutable(nullptr, 0);
    CFStringAppendCharacters(s, buf, len);
    CFStringTransform(s, nullptr, kCFStringTransformMandarinLatin, false);
    CFStringTransform(s, nullptr, kCFStringTransformStripCombiningMarks, false);
    std::string out;
    const char* c = CFStringGetCStringPtr(s, kCFStringEncodingUTF8);
    if (c != nullptr) {
        out = c;
    } else {
        char tmp[64];
        if (CFStringGetCString(s, tmp, sizeof(tmp), kCFStringEncodingUTF8)) out = tmp;
    }
    CFRelease(s);
    // 去空白、转小写
    std::string res;
    for (char ch : out) {
        if (ch == ' ' || ch == '\t') continue;
        res += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return res;
}

}  // namespace

openlyrics::ReadingLookup makeReadingLookup() {
    auto cache = std::make_shared<std::unordered_map<char32_t, std::string>>();
    auto mtx = std::make_shared<std::mutex>();
    return [cache, mtx](char32_t cp) -> std::vector<std::string> {
        if (const auto* p = openlyrics::polyphonicReadings(cp)) return *p;
        {
            std::lock_guard<std::mutex> lk(*mtx);
            auto it = cache->find(cp);
            if (it != cache->end()) {
                return it->second.empty() ? std::vector<std::string>{}
                                          : std::vector<std::string>{it->second};
            }
        }
        std::string r = transformSingleHanzi(cp);
        {
            std::lock_guard<std::mutex> lk(*mtx);
            (*cache)[cp] = r;
        }
        if (r.empty()) return {};
        return {r};
    };
}

}  // namespace openlyrics_platform
