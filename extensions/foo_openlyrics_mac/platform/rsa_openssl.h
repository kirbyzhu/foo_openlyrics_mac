// rsa_openssl.h —— 裸 RSA 模幂运算声明
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// 裸 RSA 加密：所有参数和返回值均为 hex 字符串。返回 malloc 的字符串，调用方 free。失败返回 NULL。
char* rsa_raw_encrypt(const char* plainHex, const char* modulusHex, const char* exponentHex);
#ifdef __cplusplus
}
#endif
