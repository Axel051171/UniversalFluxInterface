/**
 * UFI Flux Engine - LAN.CFG (stage L2)
 *
 * Parsed like UFI.CFG (ufi_dump.c): one key=value per line, leading blanks skipped, '#'
 * starts a comment (whole line or after the value), the value ends at a blank, '#', CR or
 * LF.  Keys are lower case; upper case is accepted too.  Unknown keys are ignored so that
 * newer files keep working with older firmware; a value that does not parse leaves the
 * default in place and counts in `errors` (ufi net shows it).
 *
 * The lan_key is a secret: it is never echoed (ufi net only reports whether one is set)
 * and the v2 session refuses to read LAN.CFG over LAN (ufi_v2.c).  A key longer than 64
 * characters is cut at 64 - authentication then fails instead of silently turning off.
 */

#include "ufi_lan_cfg.h"
#include <string.h>

const char lan_cfg_default_text[] =
    "# UFI LAN settings (board v1.0, W5500); read at start-up and by CFG_RELOAD\r\n"
    "# lan: on, or off (the LAN chip stays in reset)\r\n"
    "lan=on\r\n"
    "# ip: dhcp (falls back to a link-local 169.254.x.y address without a DHCP server)\r\n"
    "# or a fixed address a.b.c.d; mask, gw and dns only apply to a fixed address\r\n"
    "ip=dhcp\r\n"
    "mask=255.255.255.0\r\n"
    "gw=\r\n"
    "dns=\r\n"
    "# hostname: empty = ufi-XXXXXX from the MAC (letters, digits and '-', up to 32)\r\n"
    "hostname=\r\n"
    "# mac: empty = 02:xx:xx:xx:xx:xx derived from the STM32 unique ID\r\n"
    "mac=\r\n"
    "# TCP port of the UFI v2 session (ufi discover always asks on UDP 55100)\r\n"
    "port=55100\r\n"
    "# lan_write=1: disk and file writes over LAN, and only with the write lock switch open\r\n"
    "lan_write=0\r\n"
    "# lan_key: empty = no authentication; otherwise a client must answer an HMAC-SHA256\r\n"
    "# challenge with this key first (up to 64 characters, no blanks or '#')\r\n"
    "lan_key=\r\n";

size_t lan_cfg_default_len(void)
{
    return sizeof(lan_cfg_default_text) - 1u;
}

void lan_cfg_defaults(lan_cfg_t* c)
{
    memset(c, 0, sizeof(*c));
    c->enabled = true;
    c->dhcp = true;
    c->mask[0] = c->mask[1] = c->mask[2] = 255u;
    c->port = LAN_PORT_DEFAULT;
}

static char lower(char ch)
{
    return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
}

static bool is_end(char ch)
{
    return ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' || ch == '#' || ch == '\0';
}

/* value s[0..n) equals the lower-case word w */
static bool word_is(const char* s, size_t n, const char* w)
{
    const size_t k = strlen(w);
    if (n != k) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (lower(s[i]) != w[i]) {
            return false;
        }
    }
    return true;
}

bool lan_parse_ip(const char* s, size_t n, uint8_t ip[4])
{
    uint8_t out[4];
    size_t i = 0;
    for (unsigned part = 0; part < 4u; part++) {
        unsigned v = 0, digits = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            v = v * 10u + (unsigned)(s[i] - '0');
            if (++digits > 3u || v > 255u) {
                return false;
            }
            i++;
        }
        if (!digits) {
            return false;
        }
        out[part] = (uint8_t)v;
        if (part < 3u) {
            if (i >= n || s[i] != '.') {
                return false;
            }
            i++;
        }
    }
    if (i != n) {
        return false;
    }
    memcpy(ip, out, 4u);
    return true;
}

static int hexval(char ch)
{
    ch = lower(ch);
    return (ch >= '0' && ch <= '9') ? ch - '0' : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10 : -1;
}

/* xx:xx:xx:xx:xx:xx or with '-'; unicast and not all zero */
static bool parse_mac(const char* s, size_t n, uint8_t mac[6])
{
    if (n != 17u) {
        return false;
    }
    uint8_t out[6];
    for (unsigned i = 0; i < 6u; i++) {
        const int hi = hexval(s[3u * i]), lo = hexval(s[3u * i + 1u]);
        if (hi < 0 || lo < 0 || (i < 5u && s[3u * i + 2u] != ':' && s[3u * i + 2u] != '-')) {
            return false;
        }
        out[i] = (uint8_t)(hi * 16 + lo);
    }
    bool zero = true;
    for (unsigned i = 0; i < 6u; i++) {
        zero = zero && !out[i];
    }
    if (zero || (out[0] & 1u)) {
        return false;                   /* multicast / broadcast cannot be a station address */
    }
    memcpy(mac, out, 6u);
    return true;
}

/* RFC 1123 label: letters, digits, '-', not first or last */
static bool host_ok(const char* s, size_t n)
{
    if (!n || n > LAN_HOST_MAX || s[0] == '-' || s[n - 1u] == '-') {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        const char ch = lower(s[i]);
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) {
            return false;
        }
    }
    return true;
}

/* a host address: not 0.x, 127.x, multicast / reserved (224+) */
static bool host_ip_ok(const uint8_t ip[4])
{
    return ip[0] != 0u && ip[0] != 127u && ip[0] < 224u;
}

static bool number(const char* s, size_t n, uint32_t* v)
{
    uint32_t x = 0;
    if (!n || n > 5u) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        x = x * 10u + (uint32_t)(s[i] - '0');
    }
    *v = x;
    return true;
}

static void parse_line(lan_cfg_t* c, const char* k, size_t kn, const char* v, size_t vn)
{
    uint32_t num = 0;
    uint8_t ip[4];
    if (word_is(k, kn, "lan")) {
        if (word_is(v, vn, "on") || word_is(v, vn, "1")) c->enabled = true;
        else if (word_is(v, vn, "off") || word_is(v, vn, "0")) c->enabled = false;
        else c->errors++;
    } else if (word_is(k, kn, "ip")) {
        if (word_is(v, vn, "dhcp")) {
            c->dhcp = true;
        } else if (lan_parse_ip(v, vn, ip) && host_ip_ok(ip)) {
            c->dhcp = false;
            memcpy(c->ip, ip, 4u);
        } else {
            c->errors++;
        }
    } else if (word_is(k, kn, "mask") || word_is(k, kn, "gw") || word_is(k, kn, "dns")) {
        uint8_t* dst = (kn == 4u) ? c->mask : (kn == 2u) ? c->gw : c->dns;
        if (!vn) {
            memset(dst, 0, 4u);
            if (kn == 4u) {             /* empty mask = the default /24 */
                dst[0] = dst[1] = dst[2] = 255u;
            }
        } else if (lan_parse_ip(v, vn, ip)) {
            memcpy(dst, ip, 4u);
        } else {
            c->errors++;
        }
    } else if (word_is(k, kn, "hostname")) {
        if (!vn) {
            c->hostname[0] = '\0';
        } else if (host_ok(v, vn)) {
            for (size_t i = 0; i < vn; i++) c->hostname[i] = lower(v[i]);
            c->hostname[vn] = '\0';
        } else {
            c->errors++;
        }
    } else if (word_is(k, kn, "mac")) {
        if (!vn) c->mac_set = false;
        else if (parse_mac(v, vn, c->mac)) c->mac_set = true;
        else c->errors++;
    } else if (word_is(k, kn, "port")) {
        if (number(v, vn, &num) && num >= 1u && num <= 65535u) c->port = (uint16_t)num;
        else c->errors++;
    } else if (word_is(k, kn, "lan_write")) {
        if (word_is(v, vn, "1")) c->write = true;
        else if (word_is(v, vn, "0")) c->write = false;
        else c->errors++;
    } else if (word_is(k, kn, "lan_key")) {
        const size_t n = vn > LAN_KEY_MAX ? LAN_KEY_MAX : vn;
        memcpy(c->key, v, n);
        c->key[n] = '\0';
        c->key_len = (uint8_t)n;
    }
}

void lan_cfg_parse(lan_cfg_t* c, const char* text, size_t n)
{
    lan_cfg_defaults(c);
    size_t i = 0;
    while (i < n && text[i]) {
        while (i < n && (text[i] == ' ' || text[i] == '\t')) i++;
        const size_t k0 = i;
        while (i < n && text[i] && text[i] != '=' && text[i] != '\n' && text[i] != '#') i++;
        size_t kn = i - k0;
        while (kn && (text[k0 + kn - 1u] == ' ' || text[k0 + kn - 1u] == '\t')) kn--;
        if (i < n && text[i] == '=') {
            i++;
            while (i < n && (text[i] == ' ' || text[i] == '\t')) i++;
            const size_t v0 = i;
            while (i < n && !is_end(text[i])) i++;
            if (kn) {
                parse_line(c, &text[k0], kn, &text[v0], i - v0);
            }
        }
        while (i < n && text[i] && text[i] != '\n') i++;    /* rest of the line, comment */
        if (i < n && text[i] == '\n') i++;
    }
}

void lan_cfg_hostname(const lan_cfg_t* c, const uint8_t mac[6], char out[LAN_HOST_MAX + 1])
{
    static const char hex[] = "0123456789abcdef";
    if (c->hostname[0]) {
        memcpy(out, c->hostname, LAN_HOST_MAX + 1u);
        out[LAN_HOST_MAX] = '\0';
        return;
    }
    memcpy(out, "ufi-", 4u);
    for (unsigned i = 0; i < 3u; i++) {
        out[4u + 2u * i] = hex[mac[3u + i] >> 4];
        out[5u + 2u * i] = hex[mac[3u + i] & 15u];
    }
    out[10] = '\0';
}

size_t lan_fmt_ip(char* out, const uint8_t ip[4])
{
    size_t n = 0;
    for (unsigned i = 0; i < 4u; i++) {
        const unsigned v = ip[i];
        if (v >= 100u) out[n++] = (char)('0' + v / 100u);
        if (v >= 10u) out[n++] = (char)('0' + (v / 10u) % 10u);
        out[n++] = (char)('0' + v % 10u);
        if (i < 3u) out[n++] = '.';
    }
    out[n] = '\0';
    return n;
}

size_t lan_fmt_mac(char* out, const uint8_t mac[6])
{
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 6u; i++) {
        out[3u * i] = hex[mac[i] >> 4];
        out[3u * i + 1u] = hex[mac[i] & 15u];
        out[3u * i + 2u] = (i < 5u) ? ':' : '\0';
    }
    return 17u;
}
