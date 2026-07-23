#import "CryptoAdapter.h"

#import <Foundation/Foundation.h>
#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonDigest.h>
#include <Security/Security.h>

#include <cstring>
#include <dlfcn.h>
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

// 通过 dlopen 加载系统 libcrypto 的 BIGNUM 函数做裸 RSA 模幂运算。
// macOS kSecKeyAlgorithmRSAEncryptionRaw 实为 PKCS1 v1.5 填充，不适用于 NetEase weapi。
namespace {

struct BnCtx {
    void* m;
    BnCtx() : m(nullptr) {}
    ~BnCtx() { if (m) dlclose(m); }
    bool load() {
        if (m) return true;
        // macOS /usr/lib/libcrypto.dylib 禁止 dlopen（非稳定 ABI），只用 Homebrew 版号路径。
        m = dlopen("/opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib", RTLD_LAZY);
        return m != nullptr;
    }
    void* sym(const char* name) {
        return m ? dlsym(m, name) : nullptr;
    }
};

std::string rsaRawViaLibcrypto(const std::string& plain,
                                const std::string& modulusHex,
                                const std::string& exponentHex) {
    std::string modBytes = hexDecode(modulusHex);
    std::string expBytes = hexDecode(exponentHex);
    if (modBytes.empty() || expBytes.empty() || plain.empty()) return {};
    if (plain.size() > modBytes.size()) return {};

    static BnCtx ctx;
    if (!ctx.load()) return {};

    typedef void* (*BN_bin2bn_t)(const unsigned char*, int, void*);
    typedef void* (*BN_new_t)();
    typedef void  (*BN_free_t)(void*);
    typedef void* (*BN_CTX_new_t)();
    typedef void  (*BN_CTX_free_t)(void*);
    typedef int   (*BN_mod_exp_t)(void*, const void*, const void*, const void*, void*);
    typedef int   (*BN_bn2bin_t)(const void*, unsigned char*);
    typedef int   (*BN_num_bytes_t)(const void*);

    auto _BN_bin2bn   = (BN_bin2bn_t)  ctx.sym("BN_bin2bn");
    auto _BN_new       = (BN_new_t)      ctx.sym("BN_new");
    auto _BN_free      = (BN_free_t)     ctx.sym("BN_free");
    auto _BN_CTX_new   = (BN_CTX_new_t)  ctx.sym("BN_CTX_new");
    auto _BN_CTX_free  = (BN_CTX_free_t) ctx.sym("BN_CTX_free");
    auto _BN_mod_exp   = (BN_mod_exp_t)  ctx.sym("BN_mod_exp");
    auto _BN_bn2bin    = (BN_bn2bin_t)   ctx.sym("BN_bn2bin");
    auto _BN_num_bytes = (BN_num_bytes_t)ctx.sym("BN_num_bytes");

    if (!_BN_bin2bn || !_BN_new || !_BN_free || !_BN_mod_exp ||
        !_BN_bn2bin || !_BN_num_bytes || !_BN_CTX_new || !_BN_CTX_free)
        return {};

    void* m = _BN_bin2bn((const unsigned char*)plain.data(), (int)plain.size(), nullptr);
    void* e = _BN_bin2bn((const unsigned char*)expBytes.data(), (int)expBytes.size(), nullptr);
    void* n = _BN_bin2bn((const unsigned char*)modBytes.data(), (int)modBytes.size(), nullptr);
    void* c = _BN_new();
    void* bnctx = _BN_CTX_new();

    std::string result;
    if (m && e && n && c && bnctx) {
        if (_BN_mod_exp(c, m, e, n, bnctx) == 1) {
            int len = _BN_num_bytes(c);
            std::string cipherBytes(len, '\0');
            _BN_bn2bin(c, (unsigned char*)&cipherBytes[0]);
            size_t expectedLen = modBytes.size();
            if (cipherBytes.size() < expectedLen) {
                cipherBytes = std::string(expectedLen - cipherBytes.size(), '\x00') + cipherBytes;
            }
            result = hexEncode(cipherBytes);
        }
    }

    if (m) _BN_free(m);
    if (e) _BN_free(e);
    if (n) _BN_free(n);
    if (c) _BN_free(c);
    if (bnctx) _BN_CTX_free(bnctx);

    return result;
}

}  // namespace

std::string rsaRawEncryptImpl(const std::string& plain,
                                const std::string& modulusHex,
                                const std::string& exponentHex) {
    return rsaRawViaLibcrypto(plain, modulusHex, exponentHex);
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
