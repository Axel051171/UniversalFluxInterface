/* LAN.CFG (ufi_lan_cfg.c): defaults, the written default file, every key, bad values,
 * comment and line-ending variants, host name and address formatting. */
#include "check.h"
#include "ufi_lan_cfg.h"
#include <string.h>

static lan_cfg_t c;

static void parse(const char* s) { lan_cfg_parse(&c, s, strlen(s)); }

static bool ip_is(const uint8_t* ip, unsigned a, unsigned b, unsigned d2, unsigned e)
{
    return ip[0] == a && ip[1] == b && ip[2] == d2 && ip[3] == e;
}

int main(void)
{
    /* defaults, and the default file parses to exactly the defaults */
    lan_cfg_t d;
    lan_cfg_defaults(&d);
    CHECK(d.enabled && d.dhcp && d.port == 55100 && !d.write && !d.key_len && !d.mac_set &&
          !d.hostname[0] && ip_is(d.mask, 255, 255, 255, 0) && ip_is(d.gw, 0, 0, 0, 0), "defaults");
    lan_cfg_parse(&c, lan_cfg_default_text, lan_cfg_default_len());
    CHECK(!memcmp(&c, &d, sizeof(c)), "default file = defaults (errors %u)", c.errors);
    CHECK(strlen(lan_cfg_default_text) == lan_cfg_default_len() && lan_cfg_default_len() < 1023, "length");
    unsigned crlf = 0, lf = 0;
    for (const char* p = lan_cfg_default_text; *p; p++) {
        if (*p == '\n') { lf++; crlf += p > lan_cfg_default_text && p[-1] == '\r'; }
    }
    CHECK(lf == crlf && lf >= 10, "CRLF line ends (%u/%u)", crlf, lf);

    /* fixed address, gateway, DNS; inline comments, blanks, LF only, upper-case keys */
    parse("ip=192.168.1.50\nmask = 255.255.0.0 # site\n  GW=192.168.1.1\ndns=9.9.9.9\nport=6000\n");
    CHECK(!c.dhcp && ip_is(c.ip, 192, 168, 1, 50) && ip_is(c.mask, 255, 255, 0, 0) && ip_is(c.gw, 192, 168, 1, 1) &&
          ip_is(c.dns, 9, 9, 9, 9) && c.port == 6000 && !c.errors, "static (errors %u)", c.errors);
    parse("ip=10.0.0.7\r\nmask=\r\n");
    CHECK(!c.dhcp && ip_is(c.mask, 255, 255, 255, 0), "empty mask = /24");
    parse("ip=dhcp\r\n");
    CHECK(c.dhcp && !c.errors, "dhcp");
    parse("ip=DHCP\r\n");
    CHECK(c.dhcp && !c.errors, "DHCP upper case");

    /* addresses that are refused keep DHCP */
    const char* bad[] = {"ip=192.168.1\n", "ip=192.168.1.256\n", "ip=1.2.3.4.5\n", "ip=0.1.2.3\n", "ip=127.0.0.1\n",
                         "ip=224.0.0.1\n", "ip=255.255.255.255\n", "ip=1..2.3\n", "ip=0001.2.3.4\n", "ip=a.b.c.d\n",
                         "ip=1.2.3.4x\n", "ip=\n"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        parse(bad[i]);
        CHECK(c.dhcp && c.errors == 1, "refused: %s", bad[i]);
    }
    parse("gw=1.2.3\n");
    CHECK(c.errors == 1 && ip_is(c.gw, 0, 0, 0, 0), "bad gateway ignored");

    /* lan on/off */
    parse("lan=off\n");
    CHECK(!c.enabled, "off");
    parse("lan=0\nlan=ON\n");
    CHECK(c.enabled, "last line wins");
    parse("lan=maybe\n");
    CHECK(c.enabled && c.errors == 1, "bad lan value");

    /* MAC override */
    parse("mac=02:AB:cd:01:02:03\n");
    CHECK(c.mac_set && c.mac[0] == 2 && c.mac[1] == 0xab && c.mac[2] == 0xcd && c.mac[5] == 3, "mac ':'");
    parse("mac=0a-00-00-00-00-01\n");
    CHECK(c.mac_set && c.mac[0] == 0x0a && c.mac[5] == 1, "mac '-'");
    const char* badmac[] = {"mac=01:00:5e:00:00:01\n", "mac=ff:ff:ff:ff:ff:ff\n", "mac=00:00:00:00:00:00\n",
                            "mac=02:00:00:00:00\n", "mac=02:00:00:00:00:0g\n", "mac=02.00.00.00.00.01\n"};
    for (unsigned i = 0; i < sizeof(badmac) / sizeof(badmac[0]); i++) {
        parse(badmac[i]);
        CHECK(!c.mac_set && c.errors == 1, "refused: %s", badmac[i]);
    }
    parse("mac=\n");
    CHECK(!c.mac_set && !c.errors, "empty mac = from UID");

    /* host name */
    uint8_t mac[6] = {0x02, 0x20, 0x64, 0x02, 0xca, 0xb3};
    char h[LAN_HOST_MAX + 1];
    parse("hostname=\n");
    lan_cfg_hostname(&c, mac, h);
    CHECK(!strcmp(h, "ufi-02cab3"), "default host name %s", h);
    parse("hostname=Lab-Floppy2\n");
    lan_cfg_hostname(&c, mac, h);
    CHECK(!strcmp(h, "lab-floppy2"), "host name lower case %s", h);
    const char* badhost[] = {"hostname=-x\n", "hostname=x-\n", "hostname=a_b\n", "hostname=a.b\n",
                             "hostname=123456789012345678901234567890123\n"};
    for (unsigned i = 0; i < sizeof(badhost) / sizeof(badhost[0]); i++) {
        parse(badhost[i]);
        CHECK(!c.hostname[0] && c.errors == 1, "refused: %s", badhost[i]);
    }
    parse("hostname=12345678901234567890123456789012\n");
    CHECK(strlen(c.hostname) == 32 && !c.errors, "32 characters");

    /* port */
    const char* badport[] = {"port=0\n", "port=65536\n", "port=-1\n", "port=12a\n", "port=\n", "port=100000\n"};
    for (unsigned i = 0; i < sizeof(badport) / sizeof(badport[0]); i++) {
        parse(badport[i]);
        CHECK(c.port == 55100 && c.errors == 1, "refused: %s", badport[i]);
    }
    parse("port=65535\n");
    CHECK(c.port == 65535, "max port");

    /* write permission and key */
    parse("lan_write=1\nlan_key=s3cret!\n");
    CHECK(c.write && c.key_len == 7 && !strcmp(c.key, "s3cret!"), "write + key");
    parse("lan_write=yes\n");
    CHECK(!c.write && c.errors == 1, "lan_write only 0/1");
    parse("lan_key=abc#comment\n");
    CHECK(c.key_len == 3 && !strcmp(c.key, "abc"), "key ends at '#'");
    char longkey[128] = "lan_key=";
    for (unsigned i = 0; i < 80; i++) longkey[8 + i] = (char)('a' + i % 26);
    longkey[88] = '\n';
    parse(longkey);
    CHECK(c.key_len == 64 && c.key[63] == (char)('a' + 63 % 26) && !c.key[64], "key cut at 64, still set");
    parse("lan_key=\n");
    CHECK(!c.key_len && !c.key[0], "empty key = open");

    /* comments, unknown keys, no final newline, NUL inside, length-limited text */
    parse("# lan=off\nfuture_key=1\n=5\nlan_write=1");
    CHECK(c.enabled && c.write && !c.errors, "comment, unknown key, last line without newline");
    const char nul[] = "lan_write=1\0lan=off\n";
    lan_cfg_parse(&c, nul, sizeof(nul) - 1);
    CHECK(c.write && c.enabled, "stops at NUL");
    lan_cfg_parse(&c, "lan=off", 5);
    CHECK(c.enabled && c.errors == 1, "length limit cuts the value ('lan=o')");

    /* formatting */
    char t[20];
    const uint8_t ip[4] = {192, 168, 0, 5}, ip2[4] = {10, 255, 7, 100};
    CHECK(lan_fmt_ip(t, ip) == 11 && !strcmp(t, "192.168.0.5"), "fmt ip %s", t);
    CHECK(lan_fmt_ip(t, ip2) == 12 && !strcmp(t, "10.255.7.100"), "fmt ip %s", t);
    uint8_t back[4];
    CHECK(lan_parse_ip(t, strlen(t), back) && !memcmp(back, ip2, 4), "round trip");
    CHECK(lan_fmt_mac(t, mac) == 17 && !strcmp(t, "02:20:64:02:ca:b3"), "fmt mac %s", t);
    CHECK_SUMMARY("lan_cfg");
}
