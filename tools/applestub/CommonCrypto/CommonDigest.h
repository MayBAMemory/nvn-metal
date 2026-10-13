#pragma once
/* 最小替身（syntax_check.py） */
typedef unsigned int CC_LONG;
#define CC_SHA256_DIGEST_LENGTH 32
#define CC_SHA1_DIGEST_LENGTH 20
#define CC_MD5_DIGEST_LENGTH 16
typedef struct { unsigned int count[2], hash[8], wbuf[16]; } CC_SHA256_CTX;
int CC_SHA256_Init(CC_SHA256_CTX *); int CC_SHA256_Update(CC_SHA256_CTX *, const void *, CC_LONG); int CC_SHA256_Final(unsigned char *, CC_SHA256_CTX *);
unsigned char *CC_SHA256(const void *, CC_LONG, unsigned char *); unsigned char *CC_SHA1(const void *, CC_LONG, unsigned char *);
