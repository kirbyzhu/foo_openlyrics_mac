#import "CryptoAdapter.h"

#import <Foundation/Foundation.h>
#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonDigest.h>
#include <Security/Security.h>

#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

namespace openlyrics {

namespace {

// hex 解码。返回原始字节；hex 长度非偶数或无有效字符返回空。
std::string hexDecode(const std::string& hex) {
    if (hex.size() % 2 != 0) return {};
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        char high = hex[i];
        char low = hex[i + 1];
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int h = nib(high), l = nib(low);
        if (h < 0 || l < 0) return {};
        out.push_back(static_cast<char>((h << 4) | l));
    }
    return out;
}

// 字节串转小写 hex。
std::string hexEncode(const std::string& data) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : data) {
        oss << std::setw(2) << static_cast<int>(c);
    }
    return oss.str();
}

// ASN.1 DER 长度编码。
std::string asn1Length(size_t len) {
    if (len < 128) {
        return std::string(1, static_cast<char>(len));
    }
    // 计算所需字节数。
    size_t tmp = len;
    int nBytes = 0;
    while (tmp > 0) { tmp >>= 8; ++nBytes; }
    std::string result(1, static_cast<char>(0x80 | nBytes));
    for (int i = nBytes - 1; i >= 0; --i) {
        result.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
    }
    return result;
}

// ASN.1 INTEGER 编码（含必要前导 00 防负）。
std::string asn1Integer(const std::string& bytes) {
    std::string content = bytes;
    // 如果最高位为 1，前导 00 防止被解释为负数。
    if (!content.empty() && (static_cast<unsigned char>(content[0]) & 0x80)) {
        content = std::string(1, '\x00') + content;
    }
    return "\x02" + asn1Length(content.size()) + content;
}

// 构建 RSA 公钥的 SubjectPublicKeyInfo DER 编码。
// modulus 和 exponent 均为原始字节串（非 hex）。
std::string buildRSAPublicKeyDER(const std::string& modulus,
                                  const std::string& exponent) {
    // RSAPublicKey ::= SEQUENCE { modulus INTEGER, exponent INTEGER }
    std::string rsaKey = asn1Integer(modulus) + asn1Integer(exponent);
    rsaKey = "\x30" + asn1Length(rsaKey.size()) + rsaKey;

    // BIT STRING 包裹（unused bits = 0x00）。
    std::string bitString = std::string(1, '\x00') + rsaKey;
    bitString = "\x03" + asn1Length(bitString.size()) + bitString;

    // AlgorithmIdentifier ::= SEQUENCE { OID 1.2.840.113549.1.1.1, NULL }
    const char kRsaOid[] = "\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01";
    std::string algId = std::string("\x06\x09", 2) +
                        std::string(kRsaOid, 9) +
                        "\x05\x00";  // NULL
    algId = "\x30" + asn1Length(algId.size()) + algId;

    // SubjectPublicKeyInfo ::= SEQUENCE { algorithm AlgorithmIdentifier,
    //                                      subjectPublicKey BIT STRING }
    std::string result = algId + bitString;
    result = "\x30" + asn1Length(result.size()) + result;
    return result;
}

// 用 Security.framework 执行裸 RSA 加密。
// modulusHex/exponentHex: hex 字符串。
// plain: 明文字节串。
// 返回 hex 编码的密文，补齐到 modulus 字节长度。
std::string rsaRawEncryptImpl(const std::string& plain,
                                const std::string& modulusHex,
                                const std::string& exponentHex) {
    std::string modBytes = hexDecode(modulusHex);
    std::string expBytes = hexDecode(exponentHex);
    if (modBytes.empty() || expBytes.empty()) return {};

    // Raw RSA 加密要求明文长度等于密钥块大小。NetEase weapi 的 secretKey
    // 反转后仅 16 字节，需左补零到模数字节长度（2048 位 → 256 字节）。
    std::string paddedPlain = plain;
    if (paddedPlain.size() < modBytes.size()) {
        paddedPlain = std::string(modBytes.size() - paddedPlain.size(), '\x00') + paddedPlain;
    }

    std::string der = buildRSAPublicKeyDER(modBytes, expBytes);

    NSData* keyData = [NSData dataWithBytes:der.data() length:der.size()];
    NSDictionary* attrs = @{
        (__bridge id)kSecAttrKeyType: (__bridge id)kSecAttrKeyTypeRSA,
        (__bridge id)kSecAttrKeyClass: (__bridge id)kSecAttrKeyClassPublic,
        (__bridge id)kSecAttrKeySizeInBits: @(modBytes.size() * 8),
    };

    CFErrorRef error = NULL;
    SecKeyRef pubKey = SecKeyCreateWithData((__bridge CFDataRef)keyData,
                                              (__bridge CFDictionaryRef)attrs,
                                              &error);
    if (pubKey == NULL) {
        if (error) { CFRelease(error); }
        return {};
    }

    NSData* plainData = [NSData dataWithBytes:paddedPlain.data() length:paddedPlain.size()];
    CFDataRef cipherData = SecKeyCreateEncryptedData(
        pubKey, kSecKeyAlgorithmRSAEncryptionRaw,
        (__bridge CFDataRef)plainData, &error);

    CFRelease(pubKey);

    if (cipherData == NULL) {
        if (error) { CFRelease(error); }
        return {};
    }

    NSData* nsCipher = (__bridge_transfer NSData*)cipherData;
    std::string cipherBytes(reinterpret_cast<const char*>(nsCipher.bytes),
                            nsCipher.length);

    // 左补零到模数字节长度（NetEase 期望固定长度 hex）。
    size_t expectedLen = modBytes.size();
    if (cipherBytes.size() < expectedLen) {
        cipherBytes = std::string(expectedLen - cipherBytes.size(), '\x00') + cipherBytes;
    }

    return hexEncode(cipherBytes);
}

// CCCrypt 通用封装。
enum Op { kEncrypt, kDecrypt };

std::string ccCrypt(Op op, CCAlgorithm alg, CCOptions options,
                     const std::string& data, const std::string& key,
                     const std::string& iv) {
    if (key.empty()) return {};

    size_t keyLen = key.size();
    // 3DES key 固定 24 字节；AES-128 key 固定 16 字节。
    if (alg == kCCAlgorithm3DES && keyLen != 24) return {};
    if (alg == kCCAlgorithmAES && keyLen != 16) return {};

    // PKCS7 padding 最多加一个完整 block。AES block=16，3DES block=8。
    size_t blockSize = (alg == kCCAlgorithmAES) ? kCCBlockSizeAES128
                                                : kCCBlockSize3DES;
    size_t outLen = data.size() + blockSize;
    std::vector<unsigned char> out(outLen);
    size_t moved = 0;

    CCCryptorStatus status = CCCrypt(
        (op == kEncrypt) ? kCCEncrypt : kCCDecrypt,
        alg, options,
        key.data(), keyLen,
        iv.empty() ? nullptr : iv.data(),
        data.data(), data.size(),
        out.data(), outLen, &moved);

    if (status != kCCSuccess) return {};
    return std::string(reinterpret_cast<char*>(out.data()), moved);
}

}  // namespace

std::string CryptoAdapter::aes128CbcEncrypt(const std::string& plain,
                                              const std::string& key,
                                              const std::string& iv) {
    return ccCrypt(kEncrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding,
                    plain, key, iv);
}

std::string CryptoAdapter::rsaRawEncrypt(const std::string& plain,
                                           const std::string& modulusHex,
                                           const std::string& exponentHex) {
    return rsaRawEncryptImpl(plain, modulusHex, exponentHex);
}

std::string CryptoAdapter::tripleDesEcbDecrypt(const std::string& cipher,
                                                 const std::string& key) {
    return ccCrypt(kDecrypt, kCCAlgorithm3DES, kCCOptionECBMode,
                    cipher, key, "");
}

std::string CryptoAdapter::md5Hex(const std::string& data) {
    unsigned char digest[CC_MD5_DIGEST_LENGTH];
    CC_MD5(data.data(), static_cast<CC_LONG>(data.size()), digest);
    return hexEncode(std::string(reinterpret_cast<char*>(digest), CC_MD5_DIGEST_LENGTH));
}

}  // namespace openlyrics
