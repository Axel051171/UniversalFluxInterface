"""Execute selected production control functions with register/transport doubles.

The functions are extracted verbatim at build time, never copied or reimplemented.
These are control-flow regressions, not a cycle-accurate STM32 simulation. Native
codecs have separate whole-translation-unit tests in run_tests.py.
"""
from pathlib import Path
import os
import re
import subprocess
import sys

from run_tests import compiler, BUILD, FW


def function(file, name):
    source = (FW / "src" / file).read_text(encoding="utf-8")
    m = re.search(r"^[\w *]+\b" + name + r"\([^;{}]*\)\s*\{.*?^\}", source, re.M | re.S)
    if not m:
        raise RuntimeError(f"cannot extract production function {file}:{name}")
    return m.group() + "\n"


def typedef(file, name):
    source = file.read_text(encoding="utf-8")
    return re.search(r"typedef (?:enum|struct)\s*(?:__packed\s*)?\{[^}]*\}\s*" + name + ";", source).group().replace('__packed','') + "\n"


def run(name, code, sources=()):
    BUILD.mkdir(exist_ok=True)
    src = BUILD / (name + ".c")
    exe = BUILD / (name + (".exe" if os.name == "nt" else ""))
    src.write_text(code, encoding="utf-8")
    if os.environ.get("UFI_ANALYZE"):
        subprocess.run([compiler(), '--analyze', '-Xanalyzer', '-analyzer-output=text',
                        '-Xanalyzer', '-analyzer-werror', '-Wno-pointer-to-int-cast',
                        '-I', str(FW/'include'), str(src)],check=True,timeout=60)
    flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if os.environ.get("UFI_SANITIZE") else []
    subprocess.run([compiler(), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-pointer-to-int-cast", "-I", str(FW / "include"), *flags,
                    str(src), *[str(FW / s) for s in sources], "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)
    print(name + ": passed", flush=True)


BASE = "#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n"
ERRORS = typedef(FW / "include/ufi_fixes.h", "ufi_error_t")


def control_tests():
    code = BASE + ERRORS + typedef(FW / "include/ufi_firmware.h", "ufi_command_t") + r'''
#include "ufi_text.h"
#define REPLY_MAX_PAYLOAD 64
static bool active, fmt_ok;
static int flush_result, dropped, motor_off, restored, led;
static int saved_timing;
static const int PIN_LED_ERR;
static int flush_all(void) { return flush_result; }
static void drop_cache(void) { dropped++; }
static void ufi_drive_motor(bool on) { if (!on) motor_off++; }
static void ufi_drive_set_timing(const int* t) { (void)t; restored++; }
static void led_set(const int* p, bool on) { (void)p; led = on; }
static bool msc_mode;
static int hpcd_USB_OTG_HS, polls, clears;
#define OTG_HS_IRQn 5
static void HAL_PCD_IRQHandler(int* p) { (void)p; polls++; }
static void NVIC_ClearPendingIRQ(int n) { (void)n; clears++; }
#define USBD_STATE_CONFIGURED 3
#define USB_TX_TIMEOUT_MS 10
static struct { void* pClassData; unsigned dev_state; } hUsbDevice;
static unsigned ipsr, tick;
static bool tx_busy;
static unsigned __get_IPSR(void) { return ipsr; }
static unsigned HAL_GetTick(void) { return tick++; }
static int cdc_tx_busy(void) { return tx_busy; }
static uint32_t cmd_len;
static uint8_t cmd_buffer[64];
'''
    code += function("ufi_usb.c", "str_add") + function("ufi_floppy.c", "ufi_floppy_end")
    code += function("ufi_usb.c", "ufi_usb_poll")
    code += function("ufi_usb.c", "cdc_wait_idle")
    code += function("ufi_usb.c", "command_min_len") + function("ufi_usb.c", "allowed_while_busy")
    code += r'''
int main(void) {
    char mem[80]; memset(mem, 0x5a, sizeof mem);
    unsigned n = str_add(mem, 0, "UFI Flux Engine v1.18-dev 1e99eda+ Oct 13 2026");
    n = str_add(mem, n, "UFI Headless v0.7");
    n = str_add(mem, n, "STM32H723");
    n = str_add(mem, n, "PSRAM 8 MB ok");
    assert(n <= 64 && mem[64] == 0x5a && mem[63] == 0);
    assert(str_add(mem, 64, "again") == 64 && mem[64] == 0x5a);
    active = fmt_ok = true; flush_result = UFI_ERR_WRITE_PROT;
    assert(ufi_floppy_end() == UFI_ERR_WRITE_PROT);
    assert(active && fmt_ok && !dropped && !motor_off && !restored && led);
    flush_result = UFI_OK;
    assert(ufi_floppy_end() == UFI_OK);
    assert(!active && !fmt_ok && dropped == 1 && motor_off == 1 && restored == 1);
    assert(ufi_floppy_end() == UFI_OK && dropped == 1);
    msc_mode = false; ufi_usb_poll(); assert(!polls);
    msc_mode = true; ufi_usb_poll(); assert(polls == 1 && clears == 1);
    assert(!cdc_wait_idle()); msc_mode = false;
    assert(!cdc_wait_idle());
    hUsbDevice.pClassData = &tick; hUsbDevice.dev_state = USBD_STATE_CONFIGURED;
    ipsr = 23; assert(!cdc_wait_idle()); ipsr = 0;
    assert(cdc_wait_idle()); tx_busy = true; tick = UINT32_MAX - 3u;
    assert(!cdc_wait_idle() && tick < 20); /* bounded across SysTick wrap */
    assert(command_min_len(UFI_CMD_WRITE_TRACK) == 7);
    assert(command_min_len(UFI_CMD_WRITE_TRACK_C) == 11);
    cmd_len = 2; cmd_buffer[1] = 1;
    assert(command_min_len(UFI_CMD_DEBUG_GPIO) == 4);
    cmd_len = 1;
    assert(allowed_while_busy(UFI_CMD_BOARD_STATUS)); cmd_len = 2;
    assert(!allowed_while_busy(UFI_CMD_BOARD_STATUS));
    assert(!allowed_while_busy(UFI_CMD_IEC_RESET) && !allowed_while_busy(UFI_CMD_MOTOR_OFF));
    return 0;
}
'''
    run("regression_control", code)


def iec_tests():
    code = BASE + ERRORS + r'''
static const char* status;
static int blocks, closes, close_result, transport_result;
static unsigned address;
static int iec_open_file(uint8_t d, uint8_t c, const char* s) {
    assert(d == address && c == 2 && strcmp(s,"#") == 0); return 0;
}
static int ufi_iec_command(uint8_t d, const char* s, uint8_t n) {
    assert(d == address && n == strlen(s)); return transport_result;
}
static int ufi_iec_read_status(uint8_t d, char* p, uint8_t n) {
    assert(d == address); snprintf(p, n, "%s", status); return (int)strlen(p);
}
static int iec_read_channel(uint8_t d, uint8_t c, uint8_t* p, uint16_t n) {
    assert(d == address && c == 2); blocks++; memset(p, 0xa5, n); return n;
}
static int iec_close_file(uint8_t d, uint8_t c) {
    assert(d == address && c == 2); closes++; return close_result;
}
'''
    code += function("ufi_iec.c", "ufi_iec_read_block_status")
    code += function("ufi_iec.c", "ufi_iec_read_block")
    code += r'''
int main(void) {
    uint8_t data[256], dos; uint16_t n;
    address = 9; status = "23,READ ERROR,18,00\r";
    assert(ufi_iec_read_block_status(9,18,0,data,&n,&dos) == 0);
    assert(dos == 23 && n == 0 && blocks == 0 && closes == 1);
    assert(ufi_iec_read_block(9,18,0,data,&n) == UFI_ERR_STORAGE);
    status = "00,OK,00,00\r";
    assert(ufi_iec_read_block_status(9,18,0,data,&n,&dos) == 0);
    assert(dos == 0 && n == 256 && blocks == 1 && data[255] == 0xa5);
    status = "garbage";
    assert(ufi_iec_read_block_status(9,18,0,data,&n,&dos) == UFI_ERR_FRAME && n == 0);
    transport_result = UFI_ERR_TIMEOUT;
    assert(ufi_iec_read_block_status(9,18,0,data,&n,&dos) == UFI_ERR_TIMEOUT);
    transport_result = 0; status = "00,OK,00,00\r"; close_result = UFI_ERR_IEC_NOACK;
    assert(ufi_iec_read_block_status(9,18,0,data,&n,&dos) == UFI_ERR_IEC_NOACK);
    assert(closes == 6);
    return 0;
}
'''
    run("regression_iec", code)


def write_tests():
    code = BASE + ERRORS + '#include "ufi_write_codec.h"\n#include "ufi_write_stream.h"\n#include "ufi_write_wave.h"\n#include "ufi_job.h"\n'
    code += '#include "ufi_verify.h"\n'
    code += typedef(FW / "include/ufi_firmware.h", "write_state_t")
    code += typedef(FW / "src/ufi_write.c", "write_context_t")
    code += r'''
static write_context_t g_write;
static verify_ref_t verify_ref;
static mfm_decoder_t verify_dec;
static uint32_t reference_started, source_index_ticks;
static uint64_t expected_sectors;
static uint8_t expected_c,expected_h,expected_n;
static uint32_t write_buffer[100000];
static uint32_t HAL_GetTick(void);
static bool reference_poll(void) {return HAL_GetTick()-reference_started<10000u;}
static struct { int state; } g_capture;
enum { CAPTURE_IDLE };
typedef struct { uint32_t DIER, SR, ARR, EGR, CCMR1, CR1, NDTR, CCR1, DCR, DMAR; } Timer;
static Timer tim2, tim3, dma1;
#define TIM2 (&tim2)
#define TIM3 (&tim3)
#define DMA1_Stream1 (&dma1)
#define TIM_DIER_CC2IE 4u
#define TIM_SR_CC2IF 4u
#define TIM_SR_CC2OF 1024u
#define TIM_DIER_UDE 256u
#define TIM_DIER_UIE 1u
#define TIM_EGR_UG 1u
#define TIM_CCMR1_OC1M 112u
#define TIM_OCMODE_PWM1 96u
#define TIM_OCMODE_PWM2 112u
#define TIM_CR1_DIR 16u
#define TIM_CCMR1_OC1PE 8u
#define TIM_DMABASE_ARR 11u
#define TIM_DMABURSTLENGTH_3TRANSFERS 512u
#define HAL_OK 0
#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET 1
#define WRITE_INDEX_TIMEOUT_MS 1000u
#define UFI_CMD_WRITE_TRACK 0x30
#define UFI_CMD_WRITE_TRACK_VERIFY 0x32
#define UFI_CMD_WRITE_TRACK_C 0x33
#define __HAL_TIM_DISABLE(h) ((h)->CR1 = 0)
#define __HAL_TIM_ENABLE(h) ((h)->CR1 = 1)
#define DMA_SxCR_HTIE 16u
/* test DMA CR aliases the register bank */
#define CR CR1
static Timer htim3;
static int hdma_tim3_up;
#include "ufi_drive_extra.h"
static drive_extra_t extra_timing={0,0,20000,0,0,0};
drive_extra_t ufi_drive_extra_get(void){return extra_timing;}
static uint32_t cfg_pre_ticks,cfg_post_ticks,active_pre_ticks,active_post_ticks,last_ndtr, now, irq_mask, write_timeout_ms=2000;
static bool post_pending, index_cut, receive_failed, locked,local_reference_ready;
#include "ufi_raw_verify.h"
static uint8_t raw_flags;
static raw_profile_t raw_source;
static unsigned events, gate;
static int last_result;
static wstream_t ws;
static wave_cursor_t wave_cursor;
static uint32_t wave_segments = 3;
static uint16_t periods[4] = {999,999,999,999}, write_ring[2 * WSTREAM_HALF];
static uint16_t* write_arr = periods;
static const int PIN_LED_FDD, PIN_FDD_WGATE;
static const struct { int port, pin; } PIN_APL_WRREQ = {0,0};
static uint32_t HAL_GetTick(void) { return now; }
static void ufi_delay_us(unsigned n) {now+=n/1000;}
static bool ufi_cancel_requested(void) {return false;}
static uint32_t __get_PRIMASK(void) { return irq_mask; }
static void __disable_irq(void) { irq_mask = 1; }
static void __set_PRIMASK(uint32_t v) { irq_mask = v; }
static int pack_deltas(void) {
    static const uint32_t d[3]={1000,1000,1000}; wave_init(&wave_cursor,d,3); return 0;
}
static bool ufi_drive_is_apple(void) { return false; }
static bool ufi_config_apple_sync(void) { return false; }
static bool ufi_board_write_locked(void) { return locked; }
static bool ufi_drive_write_protected(void) { return false; }
static int ufi_drive_seek_q(uint8_t t, uint8_t q) { (void)t; (void)q; return 0; }
static int ufi_drive_select_side(uint8_t s) { return s<2?0:UFI_ERR_BAD_ARGS; }
static uint32_t write_media_token=1;
static bool media_matches=true;
static bool ufi_drive_media_matches(uint32_t t) { return t==1 && media_matches; }
static void apple_wrdata(bool on) { (void)on; }
static void wdata_idle(void) { }
static void dma_stop(void) { }
static void ring_fill(const wstream_fill_t* f) { (void)f; }
static int HAL_DMA_Start_IT(int* h, uint32_t from, uint32_t to, uint32_t n) {
    (void)h; (void)from; (void)to; (void)n; return HAL_OK;
}
static void led_set(const int* p, bool b) { (void)p; (void)b; }
static void bus_out(const int* p, bool b) { (void)p; gate = b; }
static void HAL_GPIO_WritePin(int p,int n,int v) { (void)p; (void)n; (void)v; }
static void ufi_sched_arm(uint32_t t) { (void)t; }
static int ufi_write_verify(void) { return 0; }
static void ufi_usb_send_event(uint8_t c, int r) { (void)c; events++; last_result = r; }
'''
    # The Apple-only path references TIM2 CNT; its value is immaterial to these tests.
    code = code.replace("#define CR CR1", "#define CR CR1\n#define CNT ARR")
    code += "void ufi_write_index_handler(uint32_t t);\nvoid ufi_write_abort(void);\n"
    for name in ("ufi_write_start", "write_finish", "ufi_write_abort", "ufi_write_index_handler",
                 "write_event_cmd", "ufi_write_receive_failed", "ufi_write_data_complete", "ufi_write_service"):
        code += function("ufi_write.c", name)
    code += r'''
static void prepare(void) {
    memset(&g_write,0,sizeof g_write); g_write.state = WRITE_RECEIVING;
    g_write.cue_index = g_write.terminate_index = true;
    g_write.flux_count = 3; g_write.bytes_expected = g_write.bytes_received = 12;
}
int main(void) {
    prepare(); TIM2->SR = TIM_SR_CC2IF | TIM_SR_CC2OF;
    assert(ufi_write_start() == UFI_OK && g_write.state == WRITE_WAITING_INDEX);
    assert(TIM2->DIER & TIM_DIER_CC2IE);
    assert(!post_pending && last_ndtr == 2u * WSTREAM_HALF);
    assert(!(TIM2->SR & (TIM_SR_CC2IF | TIM_SR_CC2OF)) && irq_mask == 0 && gate == 0);
    ufi_write_index_handler(42);
    assert(g_write.state == WRITE_ACTIVE && gate);
    ufi_write_index_handler(100);
    assert(g_write.state == WRITE_ERROR && !gate && !(TIM2->DIER & TIM_DIER_CC2IE));
    ufi_write_service(); assert(events == 1 && last_result == UFI_ERR_TRACK_LONG);
    prepare(); irq_mask = 1; assert(ufi_write_start() == 0 && irq_mask == 1); irq_mask = 0;
    now += 1001; ufi_write_service();
    assert(g_write.state == WRITE_IDLE && last_result == UFI_ERR_NO_INDEX && !gate);
    prepare(); locked = true; assert(ufi_write_start() == UFI_ERR_WRITE_PROT && !gate); locked = false;
    prepare(); unsigned before = events; ufi_write_receive_failed();
    assert(events == before && receive_failed && g_write.state == WRITE_ERROR);
    ufi_write_service(); assert(events == before+1 && last_result == UFI_ERR_BUFFER_FULL);
    assert(g_write.state == WRITE_IDLE && !receive_failed);
    prepare(); assert(ufi_write_start() == 0); ufi_write_abort();
    assert(g_write.state == WRITE_IDLE && !(TIM2->DIER & TIM_DIER_CC2IE));
    prepare();media_matches=false;assert(ufi_write_start()==UFI_ERR_MEDIA_CHANGED && !gate);
    media_matches=true;prepare();assert(!ufi_write_start());ufi_write_index_handler(42);assert(gate);
    media_matches=false;ufi_write_service();assert(!gate && g_write.state==WRITE_IDLE && last_result==UFI_ERR_MEDIA_CHANGED);
    media_matches=true;
    for (unsigned cue = 0; cue < 2; cue++) for (unsigned term = 0; term < 2; term++) {
        prepare(); g_write.gw_mode = g_write.wave_mode = true;
        g_write.cue_index = cue; g_write.terminate_index = term;
        assert(ufi_write_start() == 0);
        assert(g_write.state == (cue ? WRITE_WAITING_INDEX : WRITE_ACTIVE));
        if (cue) ufi_write_index_handler(123);
        assert(g_write.state == WRITE_ACTIVE && gate);
        assert(!!(TIM2->DIER & TIM_DIER_CC2IE) == !!term);
        ufi_write_index_handler(456);
        assert(g_write.state == (term ? WRITE_COMPLETE : WRITE_ACTIVE));
        ufi_write_abort();
    }
    prepare(); g_write.wave_mode=g_write.verify_after=true;
    write_buffer[0]=write_buffer[1]=write_buffer[2]=1000;
    assert(ufi_write_start()==UFI_ERR_NOT_IMPL && !gate);
    static uint8_t sectors[11*512];
    uint32_t n=mfm_encode_amiga_track(250000,100000,0,0,11,sectors,write_buffer,100000);
    prepare();g_write.wave_mode=g_write.verify_after=true;
    g_write.flux_count=n;
    assert(ufi_write_start()==0 && !gate && verify_ref.amiga && verify_ref.n==11);
    ufi_write_abort();
    prepare();g_write.wave_mode=g_write.verify_after=true;g_write.flux_count=n;g_write.track=1;
    assert(ufi_write_start()==UFI_ERR_BAD_ARGS && !gate);
    prepare();g_write.wave_mode=g_write.verify_after=true;g_write.flux_count=n;
    expected_sectors=0x1ffe;expected_n=2;
    assert(ufi_write_start()==UFI_ERR_BAD_ARGS && !gate); /* missing declared sector 12 */
    prepare();g_write.wave_mode=g_write.verify_after=true;g_write.flux_count=n;
    expected_sectors=0xffe;
    assert(ufi_write_start()==0 && !gate);ufi_write_abort();
    return 0;
}
'''
    run("regression_write", code, ("src/ufi_write_codec.c", "src/ufi_write_stream.c", "src/ufi_write_wave.c", "src/ufi_job.c", "src/ufi_verify.c", "src/ufi_mfm.c", "src/ufi_raw_verify.c"))


def dma_layout_tests():
    code = BASE + r'''
#define CHUNK_MAX_WORDS 32768u
static uint32_t memory[2u * 1024u * 1024u];
static uint32_t* flux_store = memory;
static uint32_t* dma_scratch;
static uint32_t chunk_words, n_chunks, store_words;
'''
    code += function("ufi_flux.c", "configure_store")
    code += r'''
int main(void) {
    const uint32_t sizes[] = {224u * 1024u / 4u, 8u * 1024u * 1024u / 4u};
    for (unsigned i = 0; i < 2; i++) {
        configure_store(sizes[i]);
        assert(n_chunks >= 2 && chunk_words <= 65535u && !(chunk_words & 7u));
        assert(store_words == n_chunks * chunk_words);
        assert(dma_scratch == flux_store + store_words);
        assert(store_words + chunk_words <= sizes[i]);
        /* A full NDTR worth of writes must not corrupt the retained capture. */
        flux_store[store_words - 1] = 0x12345678;
        for (uint32_t j = 0; j < chunk_words; j++) dma_scratch[j] = j;
        assert(flux_store[store_words - 1] == 0x12345678);
    }
    return 0;
}
'''
    run("regression_dma_layout", code)


if __name__ == "__main__":
    control_tests()
    iec_tests()
    write_tests()
    dma_layout_tests()
