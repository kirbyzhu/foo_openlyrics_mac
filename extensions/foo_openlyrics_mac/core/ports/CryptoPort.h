#pragma once
#include <string>

namespace openlyrics {

// 加密原语端口。平台层用 CommonCrypto 实现。
// 仅保留 NetEase eapi 所需原语：AES-128-ECB 与 MD5。
class CryptoPort {
public:
    virtual ~CryptoPort() = default;

    // AES-128-ECB 加密，PKCS7 padding。key 为原始字节（长度 16）。
    // 返回原始密文字节，调用方自行 hex 编码。
    virtual std::string aes128EcbEncrypt(
        const std::string& plain,
        const std::string& key) = 0;

    // MD5 摘要，返回 32 字符小写 hex。
    virtual std::string md5Hex(const std::string& data) = 0;
};

}  // namespace openlyrics
