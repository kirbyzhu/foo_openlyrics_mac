#pragma once
#include <string>

namespace openlyrics {

// RFC 4648 标准 Base64 编码。返回编码后的字符串。
std::string base64Encode(const std::string& data);

}  // namespace openlyrics
