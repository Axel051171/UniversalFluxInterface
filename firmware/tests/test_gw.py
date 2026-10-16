"""Official host encoder versus production C decoder; raw read deadline tests."""
from types import SimpleNamespace
import os
import subprocess
from reference.greaseweazle_encoder import _encode_flux, _decode_flux
from test_regressions import BASE, BUILD, ERRORS, function, run


def write_finish_tests():
    code = BASE + ERRORS + r'''
enum {ACK_OUT_OF_SRAM=12, ACK_OKAY=0, ACK_WRPROT=6, ACK_NO_INDEX=2, ACK_FLUX_UNDERFLOW=5, ST_CMD=0};
static bool w_overflow, w_bad, w_cue_index=true, w_terminate_index=false, precomp=true;
static unsigned w_have, w_space, w_count=5, calls, flux_status, state, last_cmd_ms;
static unsigned cyl[1]={12}, unit, head=1;
static struct {uint16_t pre_write, post_write;} delays={100,1000};
static void ufi_write_set_precomp(bool b) {precomp=b;}
static int ufi_write_local_gw(uint8_t t,uint8_t h,uint32_t n,bool cue,bool term,uint16_t pre) {
    assert(t==12 && h==1 && n==4 && cue && !term && pre==100 && !precomp);
    calls++; return UFI_OK;
}
static void ufi_delay_us(unsigned us) {assert(us==1000);}
static unsigned HAL_GetTick(void) {return 123;}
static void send_byte(unsigned b) {assert(b==flux_status);}
'''
    code += function('ufi_gw.c', 'write_finish')
    code += r'''
int main(void) {
write_finish(); assert(calls==1 && precomp && flux_status==0 && state==ST_CMD && last_cmd_ms==123);
w_have=1; write_finish(); assert(calls==1 && flux_status==ACK_FLUX_UNDERFLOW);
w_have=0; w_space=1; write_finish(); assert(calls==1 && flux_status==ACK_FLUX_UNDERFLOW);
w_space=0; w_count=3; write_finish(); assert(calls==1 && flux_status==ACK_FLUX_UNDERFLOW);
w_overflow=true; write_finish(); assert(calls==1 && flux_status==ACK_OUT_OF_SRAM);
return 0;
}
'''
    run('regression_gw_write_finish', code)


def encoder_tests():
    code = BASE + r'''
#define GW_TICK_SHIFT 2
#define HDR 0
static uint8_t buf[1][256];
static unsigned cur, fill;
static uint32_t gw_rem, t_last;
static bool gw = true;
static void put_u32(uint8_t* p, uint32_t v) { (void)p; (void)v; }
'''
    for name in ('put_n28', 'put_delta_gw', 'put_index'):
        code += function('ufi_stream.c', name)
    code += r'''
int main(void) {
put_delta_gw(1000); t_last = 1000;
put_index(0xfffff000u - t_last);
put_delta_gw(0xfffffff0u - t_last);
assert(fill < 100);
buf[0][fill++] = 0;
for (unsigned i=0;i<fill;i++) printf("%02x", buf[0][i]);
return 0;
}
'''
    run('regression_gw_encoder', code)
    exe = BUILD / ('regression_gw_encoder' + ('.exe' if os.name == 'nt' else ''))
    wire = bytes.fromhex(subprocess.check_output([str(exe)], text=True))
    flux, indexes = _decode_flux(None, wire)
    assert flux == [250, (0xfffffff0 - 1000) // 4]
    assert indexes == [0xfffff000 // 4]


def decoder_tests():
    code = BASE + r'''
#define GW_TICK_SHIFT 2u
#define FLUXOP_SPACE 2u
#define FLUXOP_ASTABLE 3u
static uint32_t memory[4096], capacity = 4096, w_count, w_space;
static bool w_bad, w_overflow;
static unsigned w_have, w_need;
static uint8_t w_code[6];
static uint32_t* ufi_flux_store(uint32_t* words) { *words = capacity; return memory; }
'''
    for name in ('w_put', 'n28', 'w_byte'):
        code += function('ufi_gw.c', name)
    code += '\nint main(void) {\n'
    # Boundary codes, SPACE, and official ASTABLE no-flux synthesis.
    for values in ([1, 249, 250, 504, 505, 1524, 1525, 10000],
                   [0, 200, 300, 400], [20000, 111, 222, 333]):
        wire = _encode_flux(SimpleNamespace(sample_freq=68750000), values)
        expected = []
        for v in list(values) + [6875]:
            if v > 10312:
                while v >= 172:
                    expected.append(86 * 4)
                    v -= 86
            if v:
                expected.append(v * 4)
        code += '{ const uint8_t wire[] = {' + ','.join(map(str, wire)) + '};\n'
        code += 'const uint32_t expected[] = {' + ','.join(map(str, expected)) + '};\n'
        code += '''w_count = w_space = w_have = w_need = 0; w_bad = w_overflow = false;
for (unsigned i=0; i<sizeof(wire); i++) assert(w_byte(wire[i]) == (i+1<sizeof(wire)));
assert(!w_bad && !w_overflow && !w_have && !w_need && !w_space);
assert(w_count == sizeof(expected)/sizeof(expected[0]));
assert(memcmp(memory, expected, sizeof(expected)) == 0); }
'''
    code += r'''
/* Invalid N28 tag bits, forbidden zero continuation, and capacity exhaustion. */
w_count = w_space = w_have = w_need = 0; w_bad = false;
const uint8_t bad[] = {255,2,0,1,1,1};
for (unsigned i=0;i<sizeof(bad);i++) w_byte(bad[i]);
assert(w_bad);
w_bad = false; w_byte(250); w_byte(0); assert(w_bad);
w_bad = false; w_count = 0; capacity = 1;
w_byte(10); w_byte(20); assert(w_overflow && w_count == 1);
w_bad = false; w_space = UINT32_MAX >> GW_TICK_SHIFT;
w_byte(1); assert(w_bad);
return 0;
}
'''
    run('regression_gw_decoder', code)


def deadline_tests():
    code = BASE + r'''
enum {CAPTURE_RUNNING, CAPTURE_COMPLETE};
static struct {int state;} g_capture;
static bool gw_capture = true, gw_linger;
static uint64_t gw_origin64, gw_limit, gw_end, now;
static unsigned halted;
static uint64_t ufi_flux_now64(void) {return now;}
static void capture_halt(void) {halted++;}
static uint32_t __get_PRIMASK(void) {return 0;}
static void __disable_irq(void) {}
static void __set_PRIMASK(uint32_t m) {(void)m;}
'''
    code += function('ufi_flux.c', 'ufi_flux_gw_past_end')
    code += function('ufi_flux.c', 'gw_poll_deadline')
    code += r'''
int main(void) {
    gw_origin64=UINT32_MAX-99;gw_limit=200;now=gw_origin64+199;
    gw_poll_deadline();assert(!halted);
    now++;gw_poll_deadline();assert(halted==1);
    assert(!ufi_flux_gw_past_end(99) && ufi_flux_gw_past_end(100));
    halted=0;g_capture.state=CAPTURE_RUNNING;gw_limit=(uint64_t)UINT32_MAX*4;
    now=gw_origin64+gw_limit-1;gw_poll_deadline();assert(!halted);
    now++;gw_poll_deadline();assert(halted==1);
    halted=0;g_capture.state=CAPTURE_RUNNING;gw_limit=0;
    gw_linger=true;gw_end=now+100;now+=99;gw_poll_deadline();assert(!halted);
    now++;gw_poll_deadline();assert(halted==1);
    return 0;
}
'''
    run('regression_gw_deadline', code)


if __name__ == '__main__':
    write_finish_tests()
    encoder_tests()
    decoder_tests()
    deadline_tests()
