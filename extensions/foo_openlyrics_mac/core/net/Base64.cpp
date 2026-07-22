#include "Base64.h"

namespace openlyrics {

namespace {
const char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64Encode(const std::string& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        unsigned char a = static_cast<unsigned char>(data[i]);
        unsigned char b = (i + 1 < data.size()) ? static_cast<unsigned char>(data[i + 1]) : 0;
        unsigned char c = (i + 2 < data.size()) ? static_cast<unsigned char>(data[i + 2]) : 0;
        out.push_back(kAlphabet[a >> 2]);
        out.push_back(kAlphabet[((a & 0x03) << 4) | (b >> 4)]);
        out.push_back((i + 1 < data.size()) ? kAlphabet[((b & 0x0F) << 2) | (c >> 6)] : '=');
        out.push_back((i + 2 < data.size()) ? kAlphabet[c & 0x3F] : '=');
    }
    return out;
}

}  // namespace openlyrics
