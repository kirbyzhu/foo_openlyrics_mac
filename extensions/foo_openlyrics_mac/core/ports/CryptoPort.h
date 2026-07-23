#pragma once
#include <string>

namespace openlyrics {

// 加密原语端口。平台层用 CommonCrypto/Security.framework 实现。
class CryptoPort {
public:
    virtual ~CryptoPort() = default;

    // AES-128-CBC 加密，PKCS7 padding。key/iv 为原始字节（长度 16）。
    // 返回原始密文字节，调用方自行 base64 或 hex 编码。
    virtual std::string aes128CbcEncrypt(
        const std::string& plain,
        const std::string& key,
        const std::string& iv) = 0;

    // AES-128-ECB 加密，PKCS7 padding。key 为原始字节（长度 16）。
    // 返回原始密文字节，调用方自行 hex 编码。
    virtual std::string aes128EcbEncrypt(
        const std::string& plain,
        const std::string& key) = 0;

    // 裸 RSA 加密（无 padding）：cipher = plain^e mod n。
    // modulusHex: 小写 hex 字符串，256 字符（128 字节），含前导 00。
    // exponentHex: 小写 hex 字符串，如 "010001"（65537）。
    // plain: 原始明文字节串（NetEase weapi 中为反转后的随机 key）。
    // 返回值：小写 hex 字符串，补齐到 modulus 字节长度（256 字符）。
    virtual std::string rsaRawEncrypt(
        const std::string& plain,
        const std::string& modulusHex,
        const std::string& exponentHex) = 0;

    // 3DES-ECB 解密，无 padding（调用方自行处理补齐）。
    virtual std::string tripleDesEcbDecrypt(
        const std::string& cipher,
        const std::string& key) = 0;

    // MD5 摘要，返回 32 字符小写 hex。
    virtual std::string md5Hex(const std::string& data) = 0;
};

}  // namespace openlyrics
