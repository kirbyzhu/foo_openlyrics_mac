#pragma once
#include <string>
namespace openlyrics {
// 从一个 JSON 对象的顶层取字段。仅支持顶层键（LrcLib 响应是扁平对象）。
// 找到 "key" 的字符串值 → 反转义写入 out，返回 true；键不存在/值非字符串/值为 null → false。
bool jsonGetString(const std::string& json, const std::string& key, std::string& out);
// 值为 true/false → 写 out 返回 true；否则 false。
bool jsonGetBool(const std::string& json, const std::string& key, bool& out);
}
