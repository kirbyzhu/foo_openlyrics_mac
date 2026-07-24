#import "CryptoAdapter.h"

#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonDigest.h>

#include <iomanip>
#include <sstream>
#include <vector>

namespace openlyrics {

namespace {

// 字节串转小写 hex。
std::string hexEncode(const std::string& data) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : data) {
        oss << std::setw(2) << static_cast<int>(c);
    }
    return oss.str();
}

}  // namespace

std::string CryptoAdapter::aes128EcbEncrypt(const std::string& plain,
                                              const std::string& key) {
    if (key.size() != 16) return {};

    // PKCS7 padding 最多加一个完整 block（AES block = 16）。
    size_t outLen = plain.size() + kCCBlockSizeAES128;
    std::vector<unsigned char> out(outLen);
    size_t moved = 0;

    CCCryptorStatus status = CCCrypt(
        kCCEncrypt, kCCAlgorithmAES, kCCOptionECBMode | kCCOptionPKCS7Padding,
        key.data(), key.size(), nullptr,
        plain.data(), plain.size(),
        out.data(), outLen, &moved);

    if (status != kCCSuccess) return {};
    return std::string(reinterpret_cast<char*>(out.data()), moved);
}

std::string CryptoAdapter::md5Hex(const std::string& data) {
    unsigned char digest[CC_MD5_DIGEST_LENGTH];
    CC_MD5(data.data(), static_cast<CC_LONG>(data.size()), digest);
    return hexEncode(std::string(reinterpret_cast<char*>(digest), CC_MD5_DIGEST_LENGTH));
}

}  // namespace openlyrics
