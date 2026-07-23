#pragma once
#include <string>
namespace openlyrics {
// 从一个 JSON 对象的顶层取字段。仅支持顶层键（LrcLib 响应是扁平对象）。
// 找到 "key" 的字符串值 → 反转义写入 out，返回 true；键不存在/值非字符串/值为 null → false。
bool jsonGetString(const std::string& json, const std::string& key, std::string& out);
// 值为 true/false → 写 out 返回 true；否则 false。
bool jsonGetBool(const std::string& json, const std::string& key, bool& out);
// 值为整数 → 写 out 返回 true；否则 false。
bool jsonGetInt(const std::string& json, const std::string& key, int64_t& out);
// 值为 JSON 对象 → 取其原始文本（不含外层引号）写入 out 返回 true；否则 false。
// 可用于链式取值：jsonGetObject(resp, "lrc", obj) → jsonGetString(obj, "lyric", text)。
bool jsonGetObject(const std::string& json, const std::string& key, std::string& out);
// 从 json[pos] 开始提取完整 JSON 对象（正确处理字符串内花括号）。
// pos 更新为对象 } 之后的位置，out 接收原始文本（不含外层）。成功返回 true。
bool jsonExtractObject(const std::string& json, size_t& pos, std::string& out);
// 转义字符串使其可安全嵌入 JSON 字符串字面量（不含外层引号）。
// 处理 " \ 及控制字符（\b\f\n\r\t 及 \u00XX），UTF-8 多字节原样透传。
// 用于拼接请求体，避免歌名/艺人含引号导致 JSON 畸形。
std::string jsonEscapeString(const std::string& s);
}
