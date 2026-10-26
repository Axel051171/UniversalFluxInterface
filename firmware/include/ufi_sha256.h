#pragma once
/* SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104), own implementation for the LAN
 * stage: MAC derivation from the STM32 unique ID, lan_key challenge, nonce mixing. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHA256_SIZE     32u
#define SHA256_BLOCK    64u

typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t buf[SHA256_BLOCK];
    uint32_t used;
} sha256_t;

void sha256_init(sha256_t* c);
void sha256_update(sha256_t* c, const void* data, size_t n);
void sha256_final(sha256_t* c, uint8_t out[SHA256_SIZE]);
void sha256(const void* data, size_t n, uint8_t out[SHA256_SIZE]);
void hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len,
                 uint8_t out[SHA256_SIZE]);
/* Constant-time comparison (no early exit on the first differing byte). */
bool sha256_equal(const uint8_t* a, const uint8_t* b, size_t n);
