// rsa_openssl.c —— 裸 RSA 模幂运算封装，链接 libcrypto。
// macOS Security framework 的 kSecKeyAlgorithmRSAEncryptionRaw 实为 PKCS1 填充，
// 不适用于 NetEase weapi 的裸 RSA。本文件编译为 C 直接调用 OpenSSL BIGNUM。
#include <openssl/bn.h>
#include <string.h>
#include <stdlib.h>

// hex 解码：返回 malloc 的字节串，*outLen 为长度。调用方 free。
static unsigned char* hexDecode(const char* hex, size_t* outLen) {
    size_t len = strlen(hex);
    if (len % 2 != 0) { *outLen = 0; return NULL; }
    size_t n = len / 2;
    unsigned char* out = (unsigned char*)malloc(n);
    if (!out) { *outLen = 0; return NULL; }
    for (size_t i = 0; i < n; ++i) {
        char high = hex[i*2], low = hex[i*2+1];
        int h = (high >= '0' && high <= '9') ? high - '0' :
                (high >= 'a' && high <= 'f') ? high - 'a' + 10 :
                (high >= 'A' && high <= 'F') ? high - 'A' + 10 : -1;
        int l = (low >= '0' && low <= '9') ? low - '0' :
                (low >= 'a' && low <= 'f') ? low - 'a' + 10 :
                (low >= 'A' && low <= 'F') ? low - 'A' + 10 : -1;
        if (h < 0 || l < 0) { free(out); *outLen = 0; return NULL; }
        out[i] = (unsigned char)((h << 4) | l);
    }
    *outLen = n;
    return out;
}

// hex 编码：返回 malloc 的字符串，调用方 free。
static char* hexEncode(const unsigned char* data, size_t len) {
    char* out = (char*)malloc(len * 2 + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < len; ++i) {
        static const char* hexChars = "0123456789abcdef";
        out[i*2] = hexChars[data[i] >> 4];
        out[i*2+1] = hexChars[data[i] & 0x0F];
    }
    out[len*2] = '\0';
    return out;
}

// 裸 RSA 加密：c = plain^exponent mod modulus。所有参数和返回值均为 hex 字符串。
// 调用方 free 返回值。失败返回 NULL。
char* rsa_raw_encrypt(const char* plainHex,
                      const char* modulusHex,
                      const char* exponentHex) {
    size_t plainLen, modLen, expLen;
    unsigned char* plain = hexDecode(plainHex, &plainLen);
    unsigned char* mod = hexDecode(modulusHex, &modLen);
    unsigned char* exp = hexDecode(exponentHex, &expLen);
    if (!plain || !mod || !exp) {
        free(plain); free(mod); free(exp);
        return NULL;
    }

    BIGNUM* m = BN_bin2bn(plain, (int)plainLen, NULL);
    BIGNUM* e = BN_bin2bn(exp, (int)expLen, NULL);
    BIGNUM* n = BN_bin2bn(mod, (int)modLen, NULL);
    BIGNUM* c = BN_new();
    BN_CTX* ctx = BN_CTX_new();

    char* result = NULL;
    if (m && e && n && c && ctx) {
        if (BN_mod_exp(c, m, e, n, ctx) == 1) {
            int len = BN_num_bytes(c);
            unsigned char* cipherBytes = (unsigned char*)malloc((size_t)len);
            if (cipherBytes) {
                BN_bn2bin(c, cipherBytes);
                size_t expectedLen = modLen;
                size_t outSize = len < (int)expectedLen ? expectedLen : (size_t)len;
                unsigned char* padded = (unsigned char*)calloc(outSize, 1);
                if (padded) {
                    memcpy(padded + outSize - len, cipherBytes, (size_t)len);
                    result = hexEncode(padded, outSize);
                    free(padded);
                }
                free(cipherBytes);
            }
        }
    }

    if (m) BN_free(m);
    if (e) BN_free(e);
    if (n) BN_free(n);
    if (c) BN_free(c);
    if (ctx) BN_CTX_free(ctx);
    free(plain); free(mod); free(exp);
    return result;
}
