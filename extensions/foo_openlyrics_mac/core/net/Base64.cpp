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

namespace {
unsigned char decodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<unsigned char>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<unsigned char>(c - 'a' + 26);
    if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0' + 52);
    if (c == '+') return 62;
    if (c == '/') return 63;
    return 255;  // 非法字符（含 '='）
}
}  // namespace

std::string base64Decode(const std::string& data) {
    std::string out;
    out.reserve((data.size() / 4) * 3);
    for (size_t i = 0; i + 3 < data.size(); i += 4) {
        unsigned char a = decodeChar(data[i]);
        unsigned char b = decodeChar(data[i + 1]);
        unsigned char c = decodeChar(data[i + 2]);
        unsigned char d = decodeChar(data[i + 3]);
        if (a == 255 || b == 255) return {};
        out.push_back(static_cast<char>((a << 2) | (b >> 4)));
        if (c != 255) {
            out.push_back(static_cast<char>((b << 4) | (c >> 2)));
            if (d != 255) {
                out.push_back(static_cast<char>((c << 6) | d));
            }
        }
    }
    return out;
}

}  // namespace openlyrics
