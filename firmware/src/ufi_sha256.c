/**
 * UFI Flux Engine - SHA-256 and HMAC-SHA256
 *
 * Straight from FIPS 180-4 section 6.2 (message schedule of 64 words, eight working
 * variables) and RFC 2104 (inner and outer padded key).  Small and table-free apart from
 * the 64 round constants; speed is irrelevant for one MAC at boot and one challenge per
 * LAN connection.
 */

#include "ufi_sha256.h"
#include <string.h>

/* first 32 bits of the fractional parts of the cube roots of the first 64 primes */
static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static uint32_t ror(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

static void block(sha256_t* c, const uint8_t* p)
{
    uint32_t w[64];
    for (unsigned i = 0; i < 16u; i++) {
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
               ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    }
    for (unsigned i = 16; i < 64u; i++) {
        const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
    uint32_t e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
    for (unsigned i = 0; i < 64u; i++) {
        const uint32_t t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        const uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

void sha256_init(sha256_t* c)
{
    static const uint32_t H0[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    memcpy(c->h, H0, sizeof(H0));
    c->bytes = 0;
    c->used = 0;
}

void sha256_update(sha256_t* c, const void* data, size_t n)
{
    const uint8_t* p = (const uint8_t*)data;
    c->bytes += n;
    while (n) {
        const size_t take = (SHA256_BLOCK - c->used < n) ? SHA256_BLOCK - c->used : n;
        memcpy(&c->buf[c->used], p, take);
        c->used += (uint32_t)take;
        p += take;
        n -= take;
        if (c->used == SHA256_BLOCK) {
            block(c, c->buf);
            c->used = 0;
        }
    }
}

void sha256_final(sha256_t* c, uint8_t out[SHA256_SIZE])
{
    const uint64_t bits = c->bytes * 8u;
    c->buf[c->used++] = 0x80u;
    if (c->used > SHA256_BLOCK - 8u) {
        memset(&c->buf[c->used], 0, SHA256_BLOCK - c->used);
        block(c, c->buf);
        c->used = 0;
    }
    memset(&c->buf[c->used], 0, SHA256_BLOCK - 8u - c->used);
    for (unsigned i = 0; i < 8u; i++) {
        c->buf[SHA256_BLOCK - 1u - i] = (uint8_t)(bits >> (8u * i));
    }
    block(c, c->buf);
    for (unsigned i = 0; i < 8u; i++) {
        out[4 * i] = (uint8_t)(c->h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(c->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(c->h[i] >> 8);
        out[4 * i + 3] = (uint8_t)c->h[i];
    }
}

void sha256(const void* data, size_t n, uint8_t out[SHA256_SIZE])
{
    sha256_t c;
    sha256_init(&c);
    sha256_update(&c, data, n);
    sha256_final(&c, out);
}

void hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len,
                 uint8_t out[SHA256_SIZE])
{
    uint8_t k[SHA256_BLOCK], pad[SHA256_BLOCK], inner[SHA256_SIZE];
    memset(k, 0, sizeof(k));
    if (key_len > SHA256_BLOCK) {
        sha256(key, key_len, k);        /* long keys are hashed first */
    } else if (key_len) {
        memcpy(k, key, key_len);
    }
    sha256_t c;
    for (unsigned i = 0; i < SHA256_BLOCK; i++) pad[i] = (uint8_t)(k[i] ^ 0x36u);
    sha256_init(&c);
    sha256_update(&c, pad, sizeof(pad));
    sha256_update(&c, msg, msg_len);
    sha256_final(&c, inner);
    for (unsigned i = 0; i < SHA256_BLOCK; i++) pad[i] = (uint8_t)(k[i] ^ 0x5Cu);
    sha256_init(&c);
    sha256_update(&c, pad, sizeof(pad));
    sha256_update(&c, inner, sizeof(inner));
    sha256_final(&c, out);
}

bool sha256_equal(const uint8_t* a, const uint8_t* b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}
