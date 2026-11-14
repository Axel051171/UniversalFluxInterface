"""The command table of ufi_v2.c (review item 2 of 2026-11-14).

1. Same behaviour: the table-driven functions against the five separate lists they replaced,
   kept verbatim in command_reference_2026_11_14.inc, for every request code and a set of
   payloads that covers every payload-dependent rule.  The reference is frozen history: a
   deliberate change of a rule changes the table and this test's exceptions, not the file.
2. Complete: every request code of the enum and every `case` of request() has exactly one row,
   and no row is left without a command.
"""
import re

from test_regressions import BASE, ERRORS, FW, function, run, v2_command_table

# Rules changed on purpose after the reference was frozen: {(function, cmd): reason}.  Empty today.
DELIBERATE = {}


def main():
    source = (FW / 'src' / 'ufi_v2.c').read_text(encoding='utf-8')
    enum = re.search(r'^enum \{\n    V2_PING = .*?^\};\n', source, re.M | re.S).group()
    codes = set(re.findall(r'\b(V2_[A-Z0-9_]+)\s*=', enum)) - {'V2_FRAME_ERROR'}
    table = re.search(r'^static const cmd_row_t cmd_table\[\] = \{.*?^\};\n', source, re.M | re.S).group()
    rows = re.findall(r'\{\s*((?:UFI_)?V2_[A-Z0-9_]+)\s*,', table)
    assert len(rows) == len(set(rows)), 'a command has two rows: %s' % sorted(r for r in rows if rows.count(r) > 1)
    assert set(rows) - {'UFI_V2_READ', 'UFI_V2_WRITE'} == codes, \
        'rows and request codes differ: %s' % sorted(codes ^ (set(rows) - {'UFI_V2_READ', 'UFI_V2_WRITE'}))
    assert {'UFI_V2_READ', 'UFI_V2_WRITE'} <= set(rows)
    request = function('ufi_v2.c', 'request')
    switch = request[request.index('switch (cmd)'):]
    cases = set(re.findall(r'case ((?:UFI_)?V2_[A-Z0-9_]+):', switch))
    assert cases and cases <= set(rows), 'request() handles a command without a row: %s' % sorted(cases - set(rows))
    # rows without a case: dispatched outside the switch (READ/WRITE data path, NET/AUTH special cases)?
    unhandled = set(rows) - cases
    assert all(re.search(r'\b%s\b' % r, request) or re.search(r'\b%s\b' % r, source.replace(table, ''))
               for r in unhandled), sorted(unhandled)

    code = BASE + ERRORS + r'''
#include "ufi_lan.h"
#include "ufi_lan_session.h"
enum {UFI_V2_READ=0x20,UFI_V2_WRITE=0x22};
static bool write_ok;
static bool ufi_lan_write_allowed_(void){return write_ok;}
#define ufi_lan_write_allowed ufi_lan_write_allowed_
''' + v2_command_table()
    for name in ('request_readonly', 'allowed_during_dump', 'uses_drive', 'locked_write', 'is_lan_cfg', 'lan_refusal'):
        code += function('ufi_v2.c', name)
    code += (FW / 'tests' / 'command_reference_2026_11_14.inc').read_text(encoding='utf-8')
    code += r'''
int main(void){
    /* payloads: every first byte 0..63 (analysis ops, IEC ops), third byte 0..3 (IEC_TARGET), lengths 0..4 and 8,
       and a file name for the LAN.CFG rule */
    unsigned compared=0;
    for(unsigned cmd=0;cmd<256;cmd++){
        assert(allowed_during_dump((uint8_t)cmd)==old_allowed_during_dump((uint8_t)cmd));
        assert(uses_drive((uint8_t)cmd)==old_uses_drive((uint8_t)cmd));
        for(unsigned b0=0;b0<64;b0++)for(unsigned b2=0;b2<4;b2++){
            uint8_t p[16]={(uint8_t)b0,1,(uint8_t)b2,0,0,0,'L','A','N','.','C','F','G',0,0,0};
            static const uint16_t lens[]={0,1,2,3,4,8,13};
            for(unsigned i=0;i<sizeof lens/sizeof lens[0];i++){
                const uint16_t n=lens[i];
                assert(request_readonly((uint8_t)cmd,p,n)==old_request_readonly((uint8_t)cmd,p,n));
                assert(locked_write((uint8_t)cmd,p,n)==old_locked_write((uint8_t)cmd,p,n));
                for(int w=0;w<2;w++){write_ok=w;assert(lan_refusal((uint8_t)cmd,p,n)==old_lan_refusal((uint8_t)cmd,p,n));}
                compared++;
            }
        }
    }
    assert(compared==256u*64u*4u*7u);
    /* the comparison has teeth: the reference does tell the cases apart */
    uint8_t q[8]={41,1,30,0};
    assert(!old_request_readonly(0x0F,q,4) && old_request_readonly(0x0F,q,1) && old_locked_write(0x35,(const uint8_t*)"\x01",1));
    assert(old_lan_refusal(0x04,q,0)==UFI_ERR_LAN_DENIED && old_uses_drive(0x12) && !old_uses_drive(0x00));
    /* a code without a row gets the strict defaults */
    assert(!cmd_props(0xEE,q,4) && !request_readonly(0xEE,q,0) && uses_drive(0xEE) && !allowed_during_dump(0xEE));
    puts("command table: identical to the five former lists for all 256 codes x 1792 payloads, complete and without duplicates");
    return 0;
}
'''
    run('regression_command_table', code)


if __name__ == '__main__':
    main()
