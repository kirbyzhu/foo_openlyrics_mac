#include "parser/LrcParser.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace openlyrics {
namespace {

// 尝试把 [xx:yy.zz] / [xx:yy] 解析为毫秒。成功返回 true 并写 outMs。
bool parseTimeTag(const std::string& body, int64_t& outMs) {
    // body 形如 "00:12.34" 或 "00:12" 或 "00:12.345"
    size_t colon = body.find(':');
    if (colon == std::string::npos) return false;
    std::string mm = body.substr(0, colon);
    std::string rest = body.substr(colon + 1);
    if (mm.empty() || rest.empty()) return false;
    for (char c : mm) if (!std::isdigit((unsigned char)c)) return false;

    std::string ss = rest, frac;
    size_t dot = rest.find('.');
    if (dot != std::string::npos) {
        ss = rest.substr(0, dot);
        frac = rest.substr(dot + 1);
    }
    if (ss.size() != 2) return false;
    for (char c : ss) if (!std::isdigit((unsigned char)c)) return false;
    for (char c : frac) if (!std::isdigit((unsigned char)c)) return false;

    int64_t minutes = std::stoll(mm);
    int64_t seconds = std::stoll(ss);
    int64_t fracMs = 0;
    if (!frac.empty()) {
        // 归一到毫秒：补齐/截断到 3 位
        std::string f3 = (frac + "000").substr(0, 3);
        fracMs = std::stoll(f3);
    }
    outMs = (minutes * 60 + seconds) * 1000 + fracMs;
    return true;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 验证字符串是否为有效的整数（可选的 +/- 符号后跟一个或多个数字）
bool isValidInteger(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[0] == '+' || s[0] == '-') {
        i = 1;
    }
    if (i >= s.size()) return false;  // 仅有符号，无数字
    for (size_t j = i; j < s.size(); ++j) {
        if (!std::isdigit((unsigned char)s[j])) return false;
    }
    return true;
}

}  // namespace

LyricData LrcParser::parse(const std::string& text) {
    LyricData data;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();

        // 收集本行开头连续的 [] 标签
        std::vector<int64_t> times;
        size_t pos = 0;
        bool consumed = false;
        while (pos < raw.size() && raw[pos] == '[') {
            size_t close = raw.find(']', pos);
            if (close == std::string::npos) break;
            std::string body = raw.substr(pos + 1, close - pos - 1);
            int64_t ms = 0;
            if (parseTimeTag(body, ms)) {
                times.push_back(ms);
            } else {
                // 可能是 id 标签，形如 key:value
                size_t c = body.find(':');
                if (c != std::string::npos) {
                    std::string key = body.substr(0, c);
                    std::string val = body.substr(c + 1);
                    if (key == "offset") {
                        std::string trimmedVal = trim(val);
                        if (isValidInteger(trimmedVal)) {
                            data.offsetMs = std::stoll(trimmedVal);
                        }
                        // 若非法则忽略此标签，offsetMs 保持默认值 0
                    } else {
                        data.tags.emplace_back(key, val);
                    }
                }
            }
            pos = close + 1;
            consumed = true;
        }

        std::string content = raw.substr(pos);
        if (!times.empty()) {
            for (int64_t t : times) {
                LyricLine line;
                line.timeMs = t;
                line.text = content;
                data.lines.push_back(line);
            }
        } else if (!consumed) {
            // 无任何标签，纯文本行（含空行）
            LyricLine line;
            line.timeMs = -1;
            line.text = content;
            data.lines.push_back(line);
        }
        // consumed 但无 time（纯 id 标签行）不产出歌词行
    }

    data.synced = std::any_of(data.lines.begin(), data.lines.end(),
                              [](const LyricLine& l) { return l.timeMs >= 0; });

    if (data.synced) {
        std::stable_sort(data.lines.begin(), data.lines.end(),
                         [](const LyricLine& a, const LyricLine& b) {
                             return a.timeMs < b.timeMs;
                         });
    }
    return data;
}

}  // namespace openlyrics
