#pragma once
#include "ports/CryptoPort.h"

namespace openlyrics {

// CryptoPort 的 macOS 平台实现，基于 CommonCrypto 与 Security.framework。
// 线程安全：CCCrypt/CC_MD5 可重入；SecKeyCreateEncryptedData 无全局可变状态。
class CryptoAdapter : public CryptoPort {
public:
    std::string aes128CbcEncrypt(
        const std::string& plain,
        const std::string& key,
        const std::string& iv) override;

    std::string aes128EcbEncrypt(
        const std::string& plain,
        const std::string& key) override;

    std::string rsaRawEncrypt(
        const std::string& plain,
        const std::string& modulusHex,
        const std::string& exponentHex) override;

    std::string tripleDesEcbDecrypt(
        const std::string& cipher,
        const std::string& key) override;

    std::string md5Hex(const std::string& data) override;
};

}  // namespace openlyrics
