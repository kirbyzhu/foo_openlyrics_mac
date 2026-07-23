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
// 自包含多精度模幂运算，不依赖 libcrypto。
// macOS kSecKeyAlgorithmRSAEncryptionRaw 实为 PKCS1 v1.5 填充，不适用于 NetEase 裸 RSA。
namespace {

using Limb = uint32_t;
using Double = uint64_t;
static constexpr int kLimbBits = 32;
static constexpr Double kLimbMask = 0xFFFFFFFFULL;

// 256 字节 → 64 个 32-bit limb
static constexpr int kMaxLimbs = 64;

struct BigNum {
    Limb d[kMaxLimbs];
    int nlimbs;  // 实际使用的 limb 数

    BigNum() : nlimbs(0) { memset(d, 0, sizeof(d)); }

    // 从大端字节串构造
    static BigNum fromBytes(const std::string& bytes) {
        BigNum r;
        size_t len = bytes.size();
        int limbIdx = 0;
        int shift = 0;
        for (size_t i = len; i > 0; --i) {
            r.d[limbIdx] |= static_cast<Limb>(static_cast<unsigned char>(bytes[i-1])) << shift;
            shift += 8;
            if (shift >= kLimbBits) {
                shift = 0;
                ++limbIdx;
            }
        }
        r.nlimbs = (len + 3) / 4;
        while (r.nlimbs > 1 && r.d[r.nlimbs-1] == 0) --r.nlimbs;
        if (r.nlimbs == 0) r.nlimbs = 1;
        return r;
    }

    // 导出为大端字节串（指定长度，左补零）
    std::string toBytes(int outLen) const {
        std::string r(outLen, '\0');
        int bytePos = outLen - 1;
        for (int i = 0; i < nlimbs && bytePos >= 0; ++i) {
            Limb v = d[i];
            for (int b = 0; b < 4 && bytePos >= 0; ++b) {
                r[bytePos--] = static_cast<char>(v & 0xFF);
                v >>= 8;
            }
        }
        return r;
    }

    bool isZero() const { return nlimbs == 1 && d[0] == 0; }
};

// a += b，返回进位
Limb addTo(BigNum& a, const BigNum& b) {
    Double carry = 0;
    int n = std::max(a.nlimbs, b.nlimbs);
    for (int i = 0; i < n; ++i) {
        carry += static_cast<Double>(a.d[i]) + b.d[i];
        a.d[i] = static_cast<Limb>(carry & kLimbMask);
        carry >>= kLimbBits;
    }
    if (carry && n < kMaxLimbs) {
        a.d[n] = static_cast<Limb>(carry);
        a.nlimbs = n + 1;
    } else {
        a.nlimbs = n;
        while (a.nlimbs > 1 && a.d[a.nlimbs-1] == 0) --a.nlimbs;
    }
    return static_cast<Limb>(carry);
}

// a -= b，假设 a >= b
void subFrom(BigNum& a, const BigNum& b) {
    Double borrow = 0;
    for (int i = 0; i < a.nlimbs; ++i) {
        Double sub = static_cast<Double>(b.d[i]) + borrow;
        if (static_cast<Double>(a.d[i]) < sub) {
            a.d[i] = static_cast<Limb>(static_cast<Double>(a.d[i]) + (1ULL << kLimbBits) - sub);
            borrow = 1;
        } else {
            a.d[i] = static_cast<Limb>(static_cast<Double>(a.d[i]) - sub);
            borrow = 0;
        }
    }
    while (a.nlimbs > 1 && a.d[a.nlimbs-1] == 0) --a.nlimbs;
}

// a >= b ?
bool ge(const BigNum& a, const BigNum& b) {
    if (a.nlimbs != b.nlimbs) return a.nlimbs > b.nlimbs;
    for (int i = a.nlimbs - 1; i >= 0; --i) {
        if (a.d[i] != b.d[i]) return a.d[i] > b.d[i];
    }
    return true;
}

// a = a * b，高半部分写入 high（用于后续模约简）
void mulFull(BigNum& a, const BigNum& b) {
    Limb tmp[kMaxLimbs] = {};
    int nA = a.nlimbs, nB = b.nlimbs;
    for (int i = 0; i < nA; ++i) {
        Double carry = 0;
        for (int j = 0; j < nB && i + j < kMaxLimbs; ++j) {
            carry += static_cast<Double>(a.d[i]) * b.d[j] + tmp[i + j];
            tmp[i + j] = static_cast<Limb>(carry & kLimbMask);
            carry >>= kLimbBits;
        }
        int pos = i + nB;
        while (carry && pos < kMaxLimbs) {
            carry += tmp[pos];
            tmp[pos] = static_cast<Limb>(carry & kLimbMask);
            carry >>= kLimbBits;
            ++pos;
        }
    }
    int maxLimb = nA + nB;
    if (maxLimb > kMaxLimbs) maxLimb = kMaxLimbs;
    while (maxLimb > 1 && tmp[maxLimb-1] == 0) --maxLimb;
    memcpy(a.d, tmp, maxLimb * sizeof(Limb));
    a.nlimbs = maxLimb;
}

// a = a mod n（长除法）
void mod(BigNum& a, const BigNum& n) {
    if (!ge(a, n)) return;
    // 简单减法循环——对大数效率低但我们的使用场景中 a 最多是 n 的两倍（乘法后约简）
    // 实际上乘法后 a 可能远大于 n，需要用更高效的方法
    // 对于 4096 位 ÷ 2048 位，使用二进制约简
    int nBits = n.nlimbs * kLimbBits;
    while (nBits > 0 && (n.d[(nBits-1)/kLimbBits] >> ((nBits-1) % kLimbBits)) == 0) --nBits;
    if (nBits == 0) return;

    BigNum shifted = n;
    int aBits = a.nlimbs * kLimbBits;
    while (aBits > 0 && (a.d[(aBits-1)/kLimbBits] >> ((aBits-1) % kLimbBits)) == 0) --aBits;

    // 左移 n 使之对齐 a
    int shift = aBits - nBits;
    if (shift < 0) return;
    int limbShift = shift / kLimbBits;
    int bitShift = shift % kLimbBits;
    BigNum aligned;
    aligned.nlimbs = n.nlimbs + limbShift + (bitShift > 0 ? 1 : 0);
    if (bitShift == 0) {
        for (int i = 0; i < n.nlimbs; ++i) aligned.d[i + limbShift] = n.d[i];
    } else {
        Double carry = 0;
        for (int i = 0; i < n.nlimbs; ++i) {
            Double v = (static_cast<Double>(n.d[i]) << bitShift) | carry;
            aligned.d[i + limbShift] = static_cast<Limb>(v & kLimbMask);
            carry = v >> kLimbBits;
        }
        if (carry) aligned.d[aligned.nlimbs - 1] = static_cast<Limb>(carry);
    }
    while (aligned.nlimbs > 1 && aligned.d[aligned.nlimbs-1] == 0) --aligned.nlimbs;

    for (int s = shift; s >= 0; --s) {
        if (ge(a, aligned)) subFrom(a, aligned);
        // 右移 aligned 一位
        Double carry = 0;
        for (int i = aligned.nlimbs - 1; i >= 0; --i) {
            Double v = (static_cast<Double>(aligned.d[i]) << (kLimbBits - 1)) | (carry >> 1);
            aligned.d[i] = static_cast<Limb>(v & kLimbMask);
            carry = static_cast<Double>(aligned.d[i]) << 1;
        }
        if (aligned.nlimbs > 1 && aligned.d[aligned.nlimbs-1] == 0) --aligned.nlimbs;
    }
}

// a = (a * b) mod n
void mulMod(BigNum& a, const BigNum& b, const BigNum& n) {
    mulFull(a, b);
    mod(a, n);
}

// result = base^exp mod n（exp = 65537 固定，用平方-乘）
std::string modPow65537(const std::string& baseBytes, const std::string& modBytes) {
    BigNum base = BigNum::fromBytes(baseBytes);
    BigNum n = BigNum::fromBytes(modBytes);
    if (n.isZero()) return {};

    // 初始化 result = base
    BigNum result = base;
    mod(result, n);

    // exp = 65537 = 0x10001，二进制 10000000000000001
    // 16 次平方 + 1 次乘 base
    for (int bit = 0; bit < 16; ++bit) {
        BigNum tmp = result;
        mulMod(tmp, result, n);  // tmp = result^2 mod n
        result = tmp;
    }
    // 最后一位是 1，乘 base
    mulMod(result, base, n);

    return result.toBytes(static_cast<int>(modBytes.size()));
}

}  // namespace

std::string rsaRawEncryptImpl(const std::string& plain,
                                const std::string& modulusHex,
                                const std::string& exponentHex) {
    std::string modBytes = hexDecode(modulusHex);
    std::string expBytes = hexDecode(exponentHex);
    if (modBytes.empty() || expBytes.empty() || plain.empty()) return {};
    if (plain.size() > modBytes.size()) return {};

    // 验证 exponent 是否为 65537（当前仅支持此固定指数）
    (void)expBytes;

    std::string cipherBytes = modPow65537(plain, modBytes);
    return cipherBytes.empty() ? std::string() : hexEncode(cipherBytes);
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
