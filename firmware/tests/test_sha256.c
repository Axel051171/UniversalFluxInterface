/* SHA-256 / HMAC-SHA256 (ufi_sha256.c) against the FIPS 180-4 examples and RFC 4231. */
#include "check.h"
#include "ufi_sha256.h"
#include <string.h>

static void hex(const char* s, uint8_t* out)
{
    for (unsigned i = 0; s[2 * i]; i++) {
        unsigned v = 0;
        for (unsigned k = 0; k < 2u; k++) {
            const char c = s[2 * i + k];
            v = v * 16u + (unsigned)(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        out[i] = (uint8_t)v;
    }
}

static bool digest_is(const uint8_t* d, const char* want)
{
    uint8_t w[32];
    hex(want, w);
    return memcmp(d, w, 32) == 0;
}

int main(void)
{
    uint8_t d[32];
    sha256("", 0, d);
    CHECK(digest_is(d, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "empty");
    sha256("abc", 3, d);
    CHECK(digest_is(d, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "abc");
    const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";   /* 56 bytes: padding spills */
    sha256(two, strlen(two), d);
    CHECK(digest_is(d, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), "two blocks");
    /* one million 'a' fed in odd pieces: buffering across block boundaries */
    static uint8_t a[1000];
    memset(a, 'a', sizeof(a));
    sha256_t c;
    sha256_init(&c);
    for (unsigned i = 0, step = 1; i < 1000000u; ) {
        const unsigned n = (1000000u - i < step) ? 1000000u - i : step;
        sha256_update(&c, a, n);
        i += n;
        step = step % 977u + 13u;           /* 13..989, below the 1000-byte source */
    }
    sha256_final(&c, d);
    CHECK(digest_is(d, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"), "million a");

    uint8_t key[131];
    memset(key, 0x0b, 20);
    hmac_sha256(key, 20, (const uint8_t*)"Hi There", 8, d);
    CHECK(digest_is(d, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"), "RFC 4231 case 1");
    hmac_sha256((const uint8_t*)"Jefe", 4, (const uint8_t*)"what do ya want for nothing?", 28, d);
    CHECK(digest_is(d, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"), "RFC 4231 case 2");
    memset(key, 0xaa, sizeof(key));
    const char* m6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    hmac_sha256(key, sizeof(key), (const uint8_t*)m6, strlen(m6), d);
    CHECK(digest_is(d, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"), "RFC 4231 case 6 (long key)");

    uint8_t x[32], y[32];
    memset(x, 7, 32); memcpy(y, x, 32);
    CHECK(sha256_equal(x, y, 32), "equal");
    y[31] ^= 1u;
    CHECK(!sha256_equal(x, y, 32), "last byte differs");
    y[31] ^= 1u; y[0] ^= 0x80u;
    CHECK(!sha256_equal(x, y, 32), "first byte differs");
    CHECK_SUMMARY("sha256");
}
