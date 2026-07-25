#include "config/AppConfig.h"
#include "net/JsonField.h"
#include <sstream>
#include <stdexcept>

namespace openlyrics {

// ---- helpers ----

namespace {

std::string esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

void writeSourceArray(std::ostringstream& oss, const std::vector<SourceConfig>& sources) {
    oss << "\"sources\":[";
    for (size_t i = 0; i < sources.size(); ++i) {
        if (i > 0) oss << ',';
        oss << "{\"key\":\"" << esc(sources[i].key) << "\","
            << "\"enabled\":" << (sources[i].enabled ? "true" : "false") << '}';
    }
    oss << ']';
}

void writeDisplay(std::ostringstream& oss, const DisplayConfig& d) {
    oss << "\"display\":{"
        << "\"fontName\":\"" << esc(d.fontName) << "\","
        << "\"fontSize\":" << d.fontSize << ','
        << "\"highlightScale\":" << d.highlightScale << ','
        << "\"normalColor\":\"" << esc(d.normalColor) << "\","
        << "\"highlightColor\":\"" << esc(d.highlightColor) << "\","
        << "\"alignment\":\"" << esc(d.alignment) << "\","
        << "\"lineSpacing\":" << d.lineSpacing << ','
        << "\"wordHighlight\":" << (d.wordHighlight ? "true" : "false")
        << '}';
}

// 手动解析 sources 数组：["sources":[{...},{...}]]
void parseSources(const std::string& json, std::vector<SourceConfig>& out) {
    size_t pos = json.find("\"sources\"");
    if (pos == std::string::npos) return;
    pos = json.find('[', pos);
    if (pos == std::string::npos) return;

    size_t end = json.find(']', pos);
    if (end == std::string::npos) return;

    size_t cur = pos + 1;
    while (cur < end) {
        // 跳过空白和逗号
        while (cur < end && (json[cur] == ' ' || json[cur] == '\t' ||
                              json[cur] == '\n' || json[cur] == '\r' || json[cur] == ','))
            ++cur;
        if (cur >= end || json[cur] != '{') break;

        // 找当前 {} 块（正确处理字符串内花括号）
        size_t objStart = cur;
        std::string obj;
        if (!jsonExtractObject(json, cur, obj)) break;

        SourceConfig sc;
        jsonGetString(obj, "key", sc.key);
        jsonGetBool(obj, "enabled", sc.enabled);
        if (!sc.key.empty()) out.push_back(std::move(sc));
    }
}

void parseDisplay(const std::string& json, DisplayConfig& out) {
    std::string dispObj;
    if (!jsonGetObject(json, "display", dispObj)) return;
    jsonGetString(dispObj, "fontName", out.fontName);
    int64_t fontSize = static_cast<int64_t>(out.fontSize);
    if (jsonGetInt(dispObj, "fontSize", fontSize)) out.fontSize = static_cast<double>(fontSize);
    // highlightScale: JSON number 可能是 double 也可能是整数；手动提取
    size_t pos = dispObj.find("\"highlightScale\"");
    if (pos != std::string::npos) {
        pos = dispObj.find(':', pos);
        if (pos != std::string::npos) {
            // 跳过冒号和空白
            ++pos;
            while (pos < dispObj.size() && (dispObj[pos] == ' ' || dispObj[pos] == '\t')) ++pos;
            size_t numEnd = pos;
            while (numEnd < dispObj.size() &&
                   ((dispObj[numEnd] >= '0' && dispObj[numEnd] <= '9') || dispObj[numEnd] == '.'))
                ++numEnd;
            if (numEnd > pos) {
                try { out.highlightScale = std::stod(dispObj.substr(pos, numEnd - pos)); }
                catch (const std::exception&) {}
            }
        }
    }
    jsonGetString(dispObj, "normalColor", out.normalColor);
    jsonGetString(dispObj, "highlightColor", out.highlightColor);
    jsonGetString(dispObj, "alignment", out.alignment);
    int64_t lineSpacing = static_cast<int64_t>(out.lineSpacing);
    if (jsonGetInt(dispObj, "lineSpacing", lineSpacing))
        out.lineSpacing = static_cast<double>(lineSpacing);
    jsonGetBool(dispObj, "wordHighlight", out.wordHighlight);
}

void writeDeskLyrics(std::ostringstream& oss, const DeskLyricsConfig& d) {
    oss << "\"deskLyrics\":{"
        << "\"enabled\":" << (d.enabled ? "true" : "false") << ','
        << "\"showOnlyInBackground\":" << (d.showOnlyInBackground ? "true" : "false") << ','
        << "\"fontSize\":" << d.fontSize << ','
        << "\"normalColor\":\"" << esc(d.normalColor) << "\","
        << "\"highlightColor\":\"" << esc(d.highlightColor) << "\","
        << "\"titleColor\":\"" << esc(d.titleColor) << "\","
        << "\"alignment\":\"" << esc(d.alignment) << "\","
        << "\"lineSpacing\":" << d.lineSpacing << ','
        << "\"windowWidth\":" << d.windowWidth << ','
        << "\"windowHeight\":" << d.windowHeight << ','
        << "\"windowX\":" << d.windowX << ','
        << "\"windowY\":" << d.windowY << ','
        << "\"maxLines\":" << d.maxLines << ','
        << "\"showTitle\":" << (d.showTitle ? "true" : "false")
        << '}';
}

void parseDeskLyrics(const std::string& json, DeskLyricsConfig& out) {
    std::string obj;
    if (!jsonGetObject(json, "deskLyrics", obj)) return;
    jsonGetBool(obj, "enabled", out.enabled);
    jsonGetBool(obj, "showOnlyInBackground", out.showOnlyInBackground);
    size_t pos = obj.find("\"fontSize\"");
    if (pos != std::string::npos) {
        pos = obj.find(':', pos);
        if (pos != std::string::npos) {
            ++pos;
            while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t')) ++pos;
            size_t numEnd = pos;
            while (numEnd < obj.size() &&
                   ((obj[numEnd] >= '0' && obj[numEnd] <= '9') || obj[numEnd] == '.'))
                ++numEnd;
            if (numEnd > pos) {
                try { out.fontSize = std::stod(obj.substr(pos, numEnd - pos)); }
                catch (const std::exception&) {}
            }
        }
    }
    jsonGetString(obj, "normalColor", out.normalColor);
    jsonGetString(obj, "highlightColor", out.highlightColor);
    jsonGetString(obj, "titleColor", out.titleColor);
    jsonGetString(obj, "alignment", out.alignment);
    pos = obj.find("\"lineSpacing\"");
    if (pos != std::string::npos) {
        pos = obj.find(':', pos);
        if (pos != std::string::npos) {
            ++pos;
            while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t')) ++pos;
            size_t numEnd = pos;
            while (numEnd < obj.size() &&
                   ((obj[numEnd] >= '0' && obj[numEnd] <= '9') || obj[numEnd] == '.'))
                ++numEnd;
            if (numEnd > pos) {
                try { out.lineSpacing = std::stod(obj.substr(pos, numEnd - pos)); }
                catch (const std::exception&) {}
            }
        }
    }
    // windowWidth / windowHeight: 手动提取 double（jsonGetInt 会丢小数位）
    auto parseDoubleField = [&](const char* key, double& target) {
        size_t kp = obj.find(key);
        if (kp == std::string::npos) return;
        kp = obj.find(':', kp);
        if (kp == std::string::npos) return;
        ++kp;
        while (kp < obj.size() && (obj[kp] == ' ' || obj[kp] == '\t')) ++kp;
        size_t numEnd = kp;
        while (numEnd < obj.size() &&
               ((obj[numEnd] >= '0' && obj[numEnd] <= '9') || obj[numEnd] == '.'))
            ++numEnd;
        if (numEnd > kp) {
            try { target = std::stod(obj.substr(kp, numEnd - kp)); }
            catch (const std::exception&) {}
        }
    };
    parseDoubleField("\"windowWidth\"", out.windowWidth);
    parseDoubleField("\"windowHeight\"", out.windowHeight);
    parseDoubleField("\"windowX\"", out.windowX);
    parseDoubleField("\"windowY\"", out.windowY);
    int64_t maxLines = out.maxLines;
    if (jsonGetInt(obj, "maxLines", maxLines)) out.maxLines = static_cast<int>(maxLines);
    jsonGetBool(obj, "showTitle", out.showTitle);

    // 加载后 clamp 到合法范围，防止损坏的配置导致窗口不可见
    if (out.windowWidth < 200.0) out.windowWidth = 600.0;
    if (out.windowHeight < 60.0) out.windowHeight = 120.0;
    if (out.maxLines < 3 || out.maxLines > 7) out.maxLines = 3;
}

}  // namespace

AppConfig AppConfig::defaults() {
    AppConfig c;
    c.sources = {
        {"tag", true},
        {"local", true},
        {"lrclib", true},
        {"netease", true},
        {"qqmusic", true},
    };
    return c;
}

std::string AppConfig::toJson() const {
    std::ostringstream oss;
    oss << '{';
    writeSourceArray(oss, sources);
    oss << ',';
    writeDisplay(oss, display);
    oss << ',';
    writeDeskLyrics(oss, deskLyrics);
    oss << ",\"defaultOffsetMs\":" << defaultOffsetMs
        << ",\"httpTimeoutSec\":" << httpTimeoutSec
        << ",\"maxConsecutiveFailures\":" << maxConsecutiveFailures
        << ",\"savePathTemplate\":\"" << esc(savePathTemplate) << "\""
        << ",\"logLevel\":\"" << esc(logLevel) << "\"";
    oss << '}';
    return oss.str();
}

AppConfig AppConfig::fromJson(const std::string& json) {
    AppConfig c = defaults();
    if (json.empty()) return c;

    c.sources.clear();
    parseSources(json, c.sources);
    // 如果解析后为空（JSON 中无 sources 键），回退默认值
    if (c.sources.empty()) c.sources = AppConfig::defaults().sources;
    parseDisplay(json, c.display);
    parseDeskLyrics(json, c.deskLyrics);

    int64_t v = 0;
    if (jsonGetInt(json, "defaultOffsetMs", v)) c.defaultOffsetMs = v;
    if (jsonGetInt(json, "httpTimeoutSec", v)) c.httpTimeoutSec = static_cast<int>(v);
    if (jsonGetInt(json, "maxConsecutiveFailures", v)) c.maxConsecutiveFailures = static_cast<int>(v);
    jsonGetString(json, "savePathTemplate", c.savePathTemplate);
    jsonGetString(json, "logLevel", c.logLevel);

    return c;
}

}  // namespace openlyrics
