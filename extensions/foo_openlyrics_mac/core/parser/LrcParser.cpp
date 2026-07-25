#include "parser/LrcParser.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <sstream>

namespace openlyrics {
namespace {

// 安全转换为 int64，非法或超出范围返回 false，成功写 out。
// 允许可选前导 +/- 号，其余须为十进制数字，且整串被完整消费。
bool toInt64(const std::string& s, int64_t& out) {
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        long long v = std::stoll(s, &pos);
        if (pos != s.size()) return false;  // 拒绝尾随非数字字符
        out = static_cast<int64_t>(v);
        return true;
    } catch (const std::exception&) {
        return false;  // invalid_argument 或 out_of_range
    }
}

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
    if (ss.size() < 1 || ss.size() > 2) return false;
    for (char c : ss) if (!std::isdigit((unsigned char)c)) return false;
    for (char c : frac) if (!std::isdigit((unsigned char)c)) return false;

    int64_t minutes, seconds;
    if (!toInt64(mm, minutes)) return false;
    if (!toInt64(ss, seconds)) return false;
    int64_t fracMs = 0;
    if (!frac.empty()) {
        // 归一到毫秒：补齐/截断到 3 位
        std::string f3 = (frac + "000").substr(0, 3);
        if (!toInt64(f3, fracMs)) return false;
    }
    // 防止算术溢出
    if (minutes > (INT64_MAX - seconds) / 60) return false;
    int64_t totalSec = minutes * 60 + seconds;
    if (totalSec > (INT64_MAX - fracMs) / 1000) return false;
    outMs = totalSec * 1000 + fracMs;
    return true;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 剥离字符串中所有内联/尾随的合法时标 [mm:ss.xx]，非时标方括号（如 [chorus]）原样保留。
// 用于增强型/逐字 LRC：内容中除行首时标外还嵌有多个时标时，去除这些时标本身，
// 其余字符（含空格、非时标方括号）保持不变。
std::string stripInlineTimeTags(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t pos = 0;
    while (pos < s.size()) {
        if (s[pos] == '[') {
            size_t close = s.find(']', pos);
            if (close == std::string::npos) {
                out.append(s, pos, s.size() - pos);
                break;
            }
            std::string body = s.substr(pos + 1, close - pos - 1);
            int64_t ms = 0;
            if (parseTimeTag(body, ms)) {
                // 合法时标，丢弃整个 [...]
                pos = close + 1;
                continue;
            }
            // 非时标方括号，原样保留
            out.append(s, pos, close - pos + 1);
            pos = close + 1;
        } else {
            out.push_back(s[pos]);
            ++pos;
        }
    }
    return out;
}

int64_t extractIntField(const std::string& obj, const std::string& key) {
    std::string keyStr = "\"" + key + "\":";
    size_t pos = obj.find(keyStr);
    if (pos == std::string::npos) return -1;
    pos += keyStr.size();
    while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t')) ++pos;
    size_t numEnd = pos;
    if (numEnd < obj.size() && (obj[numEnd] == '+' || obj[numEnd] == '-')) ++numEnd;
    while (numEnd < obj.size() && std::isdigit((unsigned char)obj[numEnd])) ++numEnd;
    if (numEnd > pos) {
        int64_t val = 0;
        if (toInt64(obj.substr(pos, numEnd - pos), val)) return val;
    }
    return -1;
}

// 网易云 YRC 逐字行 {"t":ms,"c":[{"tx":".."},{"tx":".."}]}：提取行首 t 毫秒与所有 tx
// 文本及逐字 syllable。
bool parseYrcLine(const std::string& raw, int64_t& outMs, std::string& outText,
                  std::vector<Syllable>& outSyllables) {
    std::string s = trim(raw);
    const std::string prefix = "{\"t\":";
    if (s.compare(0, prefix.size(), prefix) != 0) return false;
    size_t cPos = s.find("\"c\":[");
    if (cPos == std::string::npos) return false;

    size_t tp = prefix.size();
    int64_t ms = 0;
    bool any = false;
    while (tp < s.size() && std::isdigit((unsigned char)s[tp])) {
        ms = ms * 10 + (s[tp] - '0');
        ++tp;
        any = true;
    }
    if (!any) return false;

    outMs = ms;
    outText.clear();
    outSyllables.clear();

    size_t pos = cPos + 5;
    while (pos < s.size() && s[pos] != ']') {
        size_t objStart = s.find('{', pos);
        if (objStart == std::string::npos) break;

        // 先在原串上按闭引号（含转义）读取 tx 值，再据此向后定位对象结束 '}'，
        // 避免 tx 文本内出现字面量 '}' 时被误当作对象边界、导致文本截断与音节错位。
        std::string tx;
        size_t afterTx = objStart;
        const std::string txKey = "\"tx\":\"";
        size_t txPos = s.find(txKey, objStart);
        size_t naiveBrace = s.find('}', objStart);
        if (txPos != std::string::npos &&
            (naiveBrace == std::string::npos || txPos < naiveBrace)) {
            txPos += txKey.size();
            while (txPos < s.size() && s[txPos] != '"') {
                if (s[txPos] == '\\' && txPos + 1 < s.size()) {
                    tx.push_back(s[txPos + 1]);
                    txPos += 2;
                } else {
                    tx.push_back(s[txPos]);
                    ++txPos;
                }
            }
            if (txPos < s.size()) ++txPos;  // 跳过闭引号
            afterTx = txPos;
        }

        size_t objEnd = s.find('}', afterTx);
        if (objEnd == std::string::npos) break;

        std::string itemObj = s.substr(objStart, objEnd - objStart + 1);
        pos = objEnd + 1;

        int64_t li = extractIntField(itemObj, "li");
        if (li < 0) li = extractIntField(itemObj, "t");
        int64_t rc = extractIntField(itemObj, "rc");
        if (rc < 0) rc = extractIntField(itemObj, "d");

        int64_t sylStart = (li >= 0) ? li : ms;
        int64_t sylEnd = (rc > 0) ? (sylStart + rc) : 0;

        outSyllables.push_back({sylStart, sylEnd, tx});
        outText += tx;
    }

    return true;
}

std::vector<Syllable> parseInlineSyllables(int64_t lineStartMs, const std::string& s) {
    std::vector<Syllable> syllables;
    size_t pos = 0;
    int64_t curStartMs = lineStartMs;
    std::string curText;
    bool hasInlineTag = false;

    while (pos < s.size()) {
        if (s[pos] == '[') {
            size_t close = s.find(']', pos);
            if (close != std::string::npos) {
                std::string body = s.substr(pos + 1, close - pos - 1);
                int64_t tagMs = 0;
                if (parseTimeTag(body, tagMs)) {
                    hasInlineTag = true;
                    if (!curText.empty()) {
                        syllables.push_back({curStartMs, tagMs, curText});
                        curText.clear();
                    }
                    curStartMs = tagMs;
                    pos = close + 1;
                    continue;
                }
            }
            curText.push_back(s[pos]);
            ++pos;
        } else {
            curText.push_back(s[pos]);
            ++pos;
        }
    }

    if (!curText.empty() && hasInlineTag) {
        syllables.push_back({curStartMs, 0, curText});
    }

    if (!hasInlineTag) {
        return {};
    }
    return syllables;
}

// 解析 NetEase YRC / QRC 括号逐字格式行：[startMs,durationMs](sylStartMs,sylDurationMs,type)text...
bool parseYrcBracketLine(const std::string& raw, LyricLine& outLine) {
    std::string s = trim(raw);
    if (s.empty() || s[0] != '[') return false;

    size_t closeBracket = s.find(']');
    if (closeBracket == std::string::npos) return false;

    std::string header = s.substr(1, closeBracket - 1);
    size_t comma = header.find(',');
    if (comma == std::string::npos) return false;

    std::string startStr = header.substr(0, comma);
    std::string durStr = header.substr(comma + 1);

    int64_t lineStart = 0;
    int64_t lineDur = 0;
    if (!toInt64(startStr, lineStart) || !toInt64(durStr, lineDur)) return false;

    outLine.timeMs = lineStart;
    outLine.text.clear();
    outLine.syllables.clear();

    size_t pos = closeBracket + 1;
    std::string pending;  // 尚未归属任何音节的文本（首个括号组之前、或无效括号内容），并入下一音节不丢弃
    while (pos < s.size()) {
        if (s[pos] == '(') {
            size_t closeParen = s.find(')', pos);
            if (closeParen == std::string::npos) break;

            std::string sylHeader = s.substr(pos + 1, closeParen - pos - 1);
            std::stringstream ss(sylHeader);
            std::string sStartStr, sDurStr;
            std::getline(ss, sStartStr, ',');
            std::getline(ss, sDurStr, ',');

            int64_t sylStart = 0, sylDur = 0;
            if (toInt64(sStartStr, sylStart) && toInt64(sDurStr, sylDur)) {
                size_t textStart = closeParen + 1;
                size_t textEnd = s.find('(', textStart);
                if (textEnd == std::string::npos) textEnd = s.size();

                std::string sylText = pending + s.substr(textStart, textEnd - textStart);
                pending.clear();
                outLine.syllables.push_back({sylStart, sylStart + sylDur, sylText});
                outLine.text += sylText;

                pos = textEnd;
                continue;
            }
        }
        pending.push_back(s[pos]);
        ++pos;
    }

    // 尾部残留文本（无后继括号组）并入末音节，避免丢弃
    if (!pending.empty() && !outLine.syllables.empty()) {
        outLine.syllables.back().text += pending;
        outLine.text += pending;
    }

    return !outLine.syllables.empty();
}

}  // namespace

LyricData LrcParser::parse(const std::string& text) {
    LyricData data;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();

        // 网易云 YRC 逐字行 (JSON 格式)：转成带时标的标准行
        int64_t yrcMs = 0;
        std::string yrcText;
        std::vector<Syllable> yrcSyllables;
        if (parseYrcLine(raw, yrcMs, yrcText, yrcSyllables)) {
            LyricLine line;
            line.timeMs = yrcMs;
            line.text = yrcText;
            line.syllables = std::move(yrcSyllables);
            data.lines.push_back(line);
            continue;
        }

        // 网易云 YRC / QRC 括号逐字行：[startMs,durMs](sylStart,sylDur,type)text...
        LyricLine bracketLine;
        if (parseYrcBracketLine(raw, bracketLine)) {
            data.lines.push_back(std::move(bracketLine));
            continue;
        }

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
                        int64_t offsetVal = 0;
                        if (toInt64(trimmedVal, offsetVal)) {
                            data.offsetMs = offsetVal;
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
                auto syls = parseInlineSyllables(t, content);
                if (!syls.empty()) {
                    line.syllables = std::move(syls);
                    line.text.clear();
                    for (const auto& syl : line.syllables) {
                        line.text += syl.text;
                    }
                } else {
                    line.text = stripInlineTimeTags(content);
                }
                data.lines.push_back(line);
            }
        } else if (!consumed || !trim(content).empty()) {
            // 无标签、或消费了标签但有非空剩余内容时，作为纯文本行
            LyricLine line;
            line.timeMs = -1;
            line.text = content;   // 保留原始内容，仅判空时 trim
            data.lines.push_back(line);
        }
        // consumed 但无 time 且 content 为空（纯 id 标签行）不产出歌词行
    }

    data.synced = std::any_of(data.lines.begin(), data.lines.end(),
                              [](const LyricLine& l) { return l.timeMs >= 0; });

    if (data.synced) {
        std::stable_sort(data.lines.begin(), data.lines.end(),
                         [](const LyricLine& a, const LyricLine& b) {
                             return a.timeMs < b.timeMs;
                         });
    }

    data.sourceText = text;
    return data;
}

}  // namespace openlyrics
