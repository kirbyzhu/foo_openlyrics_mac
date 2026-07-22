#pragma once
#include <string>

namespace openlyrics {

// RFC 4648 标准 Base64 编解码。
std::string base64Encode(const std::string& data);
std::string base64Decode(const std::string& data);

}  // namespace openlyrics
