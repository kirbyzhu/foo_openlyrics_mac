#include "JsonField.h"

#include <cstdint>

namespace openlyrics {

namespace {

// 跳过空白字符（空格、制表、换行、回车），返回下一个非空白字符的下标。
size_t skipWhitespace(const std::string& json, size_t pos) {
    while (pos < json.size()) {
        char c = json[pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++pos;
        } else {
            break;
        }
    }
    return pos;
}

// 把一个 Unicode 码点编码为 UTF-8 追加到 out。
void appendUtf8(std::string& out, uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

// 解析 4 位十六进制（\uXXXX 之后紧跟的 4 个字符），失败返回 false。
bool parseHex4(const std::string& json, size_t pos, uint32_t& value) {
    if (pos + 4 > json.size()) return false;
    value = 0;
    for (size_t i = 0; i < 4; ++i) {
        char c = json[pos + i];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= static_cast<uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= static_cast<uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= static_cast<uint32_t>(c - 'A' + 10);
        } else {
            return false;
        }
    }
    return true;
}

// 解析一个 JSON 字符串字面量（json[pos] == '"'），反转义写入 out，
// 返回值为字符串字面量结束引号之后的下标；解析失败返回 std::string::npos。
//
// 转义处理：
// - \" \\ \/ \n \r \t \b \f 按标准含义处理。
// - \uXXXX 解码为码点后编码为 UTF-8；若是高代理（0xD800-0xDBFF）且紧跟
//   合法的低代理 \uYYYY（0xDC00-0xDFFF），按 UTF-16 代理对规则合并为一个
//   增补平面码点；否则（孤立代理，包括孤立低代理）按其原始码点值直接编码
//   为 UTF-8（即退化为“忠实保留原始码点”而非替换字符，行为确定且不丢信息）。
// - 未识别的转义 \x：反斜杠被丢弃，紧跟字符原样保留（对应任务简报要求）。
size_t parseString(const std::string& json, size_t pos, std::string& out) {
    if (pos >= json.size() || json[pos] != '"') return std::string::npos;
    ++pos;
    out.clear();
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '"') {
            return pos + 1;
        }
        if (c == '\\') {
            if (pos + 1 >= json.size()) return std::string::npos;
            char esc = json[pos + 1];
            switch (esc) {
                case '"': out.push_back('"'); pos += 2; break;
                case '\\': out.push_back('\\'); pos += 2; break;
                case '/': out.push_back('/'); pos += 2; break;
                case 'n': out.push_back('\n'); pos += 2; break;
                case 'r': out.push_back('\r'); pos += 2; break;
                case 't': out.push_back('\t'); pos += 2; break;
                case 'b': out.push_back('\b'); pos += 2; break;
                case 'f': out.push_back('\f'); pos += 2; break;
                case 'u': {
                    uint32_t cp;
                    if (!parseHex4(json, pos + 2, cp)) return std::string::npos;
                    pos += 6;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // 高代理：尝试匹配紧随其后的低代理。
                        if (pos + 1 < json.size() && json[pos] == '\\' && json[pos + 1] == 'u') {
                            uint32_t low;
                            if (parseHex4(json, pos + 2, low) && low >= 0xDC00 && low <= 0xDFFF) {
                                uint32_t combined = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                                appendUtf8(out, combined);
                                pos += 6;
                                break;
                            }
                        }
                        // 无匹配低代理：忠实保留孤立代理码点本身。
                        appendUtf8(out, cp);
                    } else {
                        appendUtf8(out, cp);
                    }
                    break;
                }
                default:
                    // 未识别转义：丢弃反斜杠，保留紧跟字符原样。
                    out.push_back(esc);
                    pos += 2;
                    break;
            }
        } else {
            out.push_back(c);
            ++pos;
        }
    }
    return std::string::npos;  // 未闭合的字符串
}

// 跳过一个 JSON 值（字符串/对象/数组/数字/true/false/null），
// 返回值结束后的下标；失败返回 std::string::npos。
// 字符串内部的引号转义与嵌套 {}/[] 的括号计数都正确处理，
// 保证不会把值内部字符串里出现的花括号/方括号误算进嵌套深度。
size_t skipValue(const std::string& json, size_t pos) {
    pos = skipWhitespace(json, pos);
    if (pos >= json.size()) return std::string::npos;
    char c = json[pos];
    if (c == '"') {
        std::string dummy;
        return parseString(json, pos, dummy);
    }
    if (c == '{' || c == '[') {
        char open = c;
        char close = (open == '{') ? '}' : ']';
        int depth = 1;
        ++pos;
        while (pos < json.size() && depth > 0) {
            char cc = json[pos];
            if (cc == '"') {
                std::string dummy;
                size_t next = parseString(json, pos, dummy);
                if (next == std::string::npos) return std::string::npos;
                pos = next;
                continue;
            }
            if (cc == open) {
                ++depth;
            } else if (cc == close) {
                --depth;
            }
            ++pos;
        }
        return (depth == 0) ? pos : std::string::npos;
    }
    if (c == 't') {
        // true
        if (json.compare(pos, 4, "true") == 0) return pos + 4;
        return std::string::npos;
    }
    if (c == 'f') {
        // false
        if (json.compare(pos, 5, "false") == 0) return pos + 5;
        return std::string::npos;
    }
    if (c == 'n') {
        // null
        if (json.compare(pos, 4, "null") == 0) return pos + 4;
        return std::string::npos;
    }
    // 数字：可选负号，数字，可选小数部分，可选指数部分。
    if (c == '-' || (c >= '0' && c <= '9')) {
        size_t start = pos;
        if (c == '-') ++pos;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') ++pos;
        if (pos < json.size() && json[pos] == '.') {
            ++pos;
            while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') ++pos;
        }
        if (pos < json.size() && (json[pos] == 'e' || json[pos] == 'E')) {
            ++pos;
            if (pos < json.size() && (json[pos] == '+' || json[pos] == '-')) ++pos;
            while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') ++pos;
        }
        if (pos == start) return std::string::npos;
        return pos;
    }
    return std::string::npos;
}

// 在顶层对象中查找 key，找到时通过 valueStart 返回值起始下标（跳过前导空白后），
// 找到返回 true；不存在返回 false。
bool findTopLevelValue(const std::string& json, const std::string& key, size_t& valueStart) {
    size_t pos = skipWhitespace(json, 0);
    if (pos >= json.size() || json[pos] != '{') return false;
    ++pos;
    pos = skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == '}') return false;  // 空对象

    while (pos < json.size()) {
        pos = skipWhitespace(json, pos);
        if (pos >= json.size() || json[pos] != '"') return false;
        std::string currentKey;
        size_t afterKey = parseString(json, pos, currentKey);
        if (afterKey == std::string::npos) return false;
        pos = skipWhitespace(json, afterKey);
        if (pos >= json.size() || json[pos] != ':') return false;
        ++pos;
        pos = skipWhitespace(json, pos);

        if (currentKey == key) {
            valueStart = pos;
            return true;
        }

        size_t afterValue = skipValue(json, pos);
        if (afterValue == std::string::npos) return false;
        pos = skipWhitespace(json, afterValue);
        if (pos < json.size() && json[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < json.size() && json[pos] == '}') {
            return false;  // 扫描到对象结尾仍未匹配
        }
        return false;  // 语法不符合预期
    }
    return false;
}

}  // namespace

bool jsonGetString(const std::string& json, const std::string& key, std::string& out) {
    size_t valueStart;
    if (!findTopLevelValue(json, key, valueStart)) return false;
    if (valueStart >= json.size() || json[valueStart] != '"') return false;
    std::string result;
    size_t end = parseString(json, valueStart, result);
    if (end == std::string::npos) return false;
    out = std::move(result);
    return true;
}

bool jsonGetBool(const std::string& json, const std::string& key, bool& out) {
    size_t valueStart;
    if (!findTopLevelValue(json, key, valueStart)) return false;
    if (json.compare(valueStart, 4, "true") == 0) {
        out = true;
        return true;
    }
    if (json.compare(valueStart, 5, "false") == 0) {
        out = false;
        return true;
    }
    return false;
}

}  // namespace openlyrics
