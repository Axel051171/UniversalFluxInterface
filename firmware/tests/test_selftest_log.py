"""ufi_selftest(): item table and SELFTEST.LOG text.

ufi_selftest() is extracted verbatim together with the status enum and the item names.  The
single items (t_timer ...) are doubles that return a preset status and value per id; their own
logic is not tested here.

1. Production buffer: ids and order of the reply, 6 bytes per item, one CR LF line per item
   in the log, the longest possible table fits, a failed file write makes the log item fail.
2. The same function with text[] cut to 200 bytes (and the _Static_assert that forbids this
   removed, in this build only): the loop stops on a line boundary and the log item warns.
3. text[] one byte below what the table needs must not compile: the _Static_assert holds.
"""
import re
import subprocess

from run_tests import compiler, BUILD
from test_regressions import ERRORS, FW, typedef, function, run

PRE = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define UFI_FW_VERSION "9.99-test"
static st_item_t preset[32];
static void fill(st_item_t* it) { assert(it->id < 32u); it->status = preset[it->id].status; it->value = preset[it->id].value; }
#define ITEM_DOUBLE(name) static void name(st_item_t* it) { fill(it); }
ITEM_DOUBLE(t_timer) ITEM_DOUBLE(t_usb_sof) ITEM_DOUBLE(t_psram) ITEM_DOUBLE(t_sd) ITEM_DOUBLE(t_board_id)
ITEM_DOUBLE(t_usbc) ITEM_DOUBLE(t_fdd_lines) ITEM_DOUBLE(t_iec_loop) ITEM_DOUBLE(t_index_loop)
ITEM_DOUBLE(t_last_reset) ITEM_DOUBLE(t_stack) ITEM_DOUBLE(t_rtc) ITEM_DOUBLE(t_sht40) ITEM_DOUBLE(t_sync)
ITEM_DOUBLE(t_wgate_lock) ITEM_DOUBLE(t_fdd_pins) ITEM_DOUBLE(t_psram_dma)
static void t_rail(st_item_t* it, int rail) { assert(it->id == 5 + rail); fill(it); }
static void t_vrail(st_item_t* it, int rail) { assert(it->id == 18 + rail); fill(it); }
static char file[1024];
static unsigned file_len, writes;
static int write_result;
static int ufi_file_write(const char* name, uint32_t offset, bool truncate, const uint8_t* data, uint16_t len, uint16_t* written)
{
    assert(!strcmp(name, "SELFTEST.LOG") && offset == 0 && truncate && len < sizeof file);
    writes++;
    if (write_result) return write_result;
    memcpy(file, data, len); file[len] = 0; file_len = len; *written = len;
    return 0;
}
static unsigned lines(void) { unsigned n = 0; for (const char* p = file; (p = strstr(p, "\r\n")) != NULL; p += 2) n++; return n; }
enum { NAMES = sizeof item_names / sizeof item_names[0] };
'''

FULL = r'''
static const uint8_t order[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18, 19, 20, 21, 22, 14};
enum { ITEMS = sizeof order };
int main(void)
{
    uint8_t out[6 * 32];
    uint32_t v;
    /* every id of the table has its own name of at most 11 characters */
    assert(NAMES == ITEMS + 1u);
    for (unsigned i = 0; i < ITEMS; i++) {
        assert(order[i] < NAMES && strlen(item_names[order[i]]) >= 1 && strlen(item_names[order[i]]) <= 11);
        for (unsigned k = 0; k < i; k++) assert(strcmp(item_names[order[i]], item_names[order[k]]));
    }
    preset[1] = (st_item_t){.status = ST_OK, .value = 275000};
    preset[4] = (st_item_t){.status = ST_FAIL};
    preset[11] = (st_item_t){.status = ST_SKIP, .value = 7};
    preset[22] = (st_item_t){.status = ST_WARN, .value = 0xFFFFFFFFu};
    memset(out, 0xEE, sizeof out);
    assert(ufi_selftest(out, 32) == ITEMS && writes == 1);
    for (unsigned i = 0; i < ITEMS; i++) assert(out[i * 6] == order[i]);
    assert(out[6 * ITEMS] == 0xEE);
    memcpy(&v, out + 2, 4); assert(out[1] == ST_OK && v == 275000);
    memcpy(&v, out + 6 * 20 + 2, 4); assert(out[6 * 20] == 22 && out[6 * 20 + 1] == ST_WARN && v == 0xFFFFFFFFu);
    /* the log: head, then one line per item, the log item itself is not in the file */
    assert(!strncmp(file, "UFI selftest firmware 9.99-test\r\n", 33));
    assert(lines() == ITEMS && file[file_len - 2] == '\r' && file[file_len - 1] == '\n');
    assert(strstr(file, "\r\ntimer       ok   275000\r\n"));
    assert(strstr(file, "\r\nsd          FAIL 0\r\n"));
    assert(strstr(file, "\r\nindex_loop  skip 7\r\n"));
    assert(strstr(file, "\r\npsram_dma   warn 4294967295\r\n") && !strstr(file, "\r\nlog "));
    memcpy(&v, out + 6 * 21 + 2, 4); assert(out[6 * 21 + 1] == ST_OK && v == file_len);

    /* the longest table: every value ten digits.  Nothing is cut, no line exceeds LOG_LINE = 29 bytes */
    for (unsigned i = 0; i < 32; i++) preset[i] = (st_item_t){.status = ST_FAIL, .value = 0xFFFFFFFFu};
    assert(ufi_selftest(out, 32) == ITEMS && lines() == ITEMS && out[6 * 21 + 1] == ST_OK);
    for (const char* p = file; *p; ) { const char* e = strstr(p, "\r\n"); assert(e && e + 2 - p <= 64); if (p != file) assert(e + 2 - p <= 29); p = e + 2; }

    /* fewer items asked for than the table has: the head of the table, the file still whole */
    memset(out, 0xEE, sizeof out);
    assert(ufi_selftest(out, 5) == 5 && out[6 * 4] == 5 && out[6 * 5] == 0xEE && lines() == ITEMS);

    /* file write refused: the log item fails with value 0 */
    write_result = UFI_ERR_STORAGE;
    assert(ufi_selftest(out, 32) == ITEMS);
    memcpy(&v, out + 6 * 21 + 2, 4); assert(out[6 * 21] == 14 && out[6 * 21 + 1] == ST_FAIL && v == 0);
    puts("selftest: item ids and order, SELFTEST.LOG one line per item, write failure reported");
    return 0;
}
'''

CUT = r'''
int main(void)
{
    uint8_t out[6 * 32];
    uint32_t v;
    assert(ufi_selftest(out, 32) == 22 && writes == 1);
    /* 200-byte buffer: the loop stops while a whole line still fits, the file ends on a line end */
    assert(file_len < 200 && file[file_len - 2] == '\r' && file[file_len - 1] == '\n');
    assert(lines() > 1 && lines() < 22);
    memcpy(&v, out + 6 * 21 + 2, 4);
    assert(out[6 * 21] == 14 && out[6 * 21 + 1] == ST_WARN && v == file_len);
    puts("selftest: a cut log is reported as a warning by the log item");
    return 0;
}
'''


def main():
    source = (FW / 'src' / 'ufi_selftest.c').read_text(encoding='utf-8')
    status = re.search(r'^enum \{\n    ST_OK.*?^\};\n', source, re.M | re.S).group()
    names = re.search(r'^static const char\* const item_names\[\] = \{.*?^\};\n', source, re.M | re.S).group()
    head = '#include <stdint.h>\n' + ERRORS + status + typedef(FW / 'src/ufi_selftest.c', 'st_item_t') + names + PRE
    selftest = function('ufi_selftest.c', 'ufi_selftest')
    assert 'char text[800];' in selftest, 'buffer size of SELFTEST.LOG changed: adjust the variants below'
    run('regression_selftest_log', head + selftest + FULL)

    # 731 bytes are needed (head 64 + 23 lines of 29), and 40 stay free behind the loop guard
    run('regression_selftest_log_771', head + selftest.replace('char text[800];', 'char text[771];') + FULL)
    BUILD.mkdir(parents=True, exist_ok=True)
    small = BUILD / 'regression_selftest_log_770.c'
    small.write_text(head + selftest.replace('char text[800];', 'char text[770];') + FULL, encoding='utf-8')
    refused = subprocess.run([compiler(), '-std=c11', '-fsyntax-only', '-I', str(FW / 'include'), str(small)],
                             capture_output=True, text=True, timeout=60)
    assert refused.returncode and 'SELFTEST.LOG buffer too small' in refused.stderr, \
        'text[770] compiled: the _Static_assert of the log buffer did not fire'
    print('regression_selftest_log_770: refused by the compiler as intended', flush=True)

    guard = re.search(r'    _Static_assert\(sizeof\(text\) - LOG_GUARD >=.*?;\n', selftest, re.S).group()
    cut = selftest.replace('char text[800];', 'char text[200];').replace(guard, '')
    run('regression_selftest_log_cut', head + cut + CUT)


if __name__ == '__main__':
    main()
