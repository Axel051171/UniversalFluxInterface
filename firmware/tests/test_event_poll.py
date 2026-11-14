"""Polled media events of ufi_v2.c: disk change 0x80 and write protect 0x86 (poll_media)."""
from test_regressions import function, run


def main():
    code = r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
typedef enum { DRIVE_NONE = 0, DRIVE_SHUGART_A, DRIVE_SHUGART_B, DRIVE_IEC, DRIVE_APPLE_II, DRIVE_APPLE2 } drive_type_t;
enum { UFI_V2_EVT_DISK = 0x80, UFI_V2_EVT_WPROT = 0x86 };
static uint32_t ev_mask;
static bool last_dc;
static uint8_t wp_drive, wp_seen;
static drive_type_t cur;
static bool dc, wp, mac;
static unsigned wp_reads;
static drive_type_t ufi_drive_get_current(void){return cur;}
static bool ufi_drive_disk_changed(void){return dc;}
static bool ufi_drive_write_protected(void){wp_reads++;return wp;}
static bool ufi_drive_is_apple(void){return cur==DRIVE_APPLE_II || cur==DRIVE_APPLE2;}
static bool ufi_drive_is_mac(void){return mac;}
static unsigned events;
static uint8_t last_code, last_len, last[8];
static void ufi_v2_event(uint8_t code, const void* payload, uint16_t len)
{
    events++; last_code = code; last_len = (uint8_t)len; memcpy(last, payload, len);
}
''' + function('ufi_v2.c', 'poll_media') + r'''
int main(void)
{
    const uint32_t WP = 1u << (UFI_V2_EVT_WPROT - UFI_V2_EVT_DISK);

    /* mask bit clear: the line is not even read */
    ev_mask = 1u; cur = DRIVE_SHUGART_A; wp = true;
    poll_media(); assert(!events && !wp_reads && !wp_seen);

    /* first sample is the baseline, also when the disk is protected */
    ev_mask = WP;
    poll_media(); assert(!events && wp_seen == 2 && wp_drive == DRIVE_SHUGART_A);
    poll_media(); assert(!events);

    /* protected -> open -> protected: one event per edge, payload drive + state */
    wp = false; poll_media();
    assert(events == 1 && last_code == UFI_V2_EVT_WPROT && last_len == 2 && last[0] == DRIVE_SHUGART_A && last[1] == 0);
    poll_media(); assert(events == 1);
    wp = true; poll_media(); assert(events == 2 && last[1] == 1);

    /* another drive: its first sample is a baseline even though the state differs */
    cur = DRIVE_SHUGART_B; wp = false; poll_media();
    assert(events == 2 && wp_drive == DRIVE_SHUGART_B && wp_seen == 1);
    wp = true; poll_media(); assert(events == 3 && last[0] == DRIVE_SHUGART_B && last[1] == 1);

    /* no drive, IEC and the Disk II drop the baseline and never read or report */
    unsigned reads = wp_reads;
    cur = DRIVE_NONE; wp = false; poll_media(); assert(events == 3 && !wp_seen);
    cur = DRIVE_IEC; poll_media(); assert(events == 3 && !wp_seen);
    cur = DRIVE_APPLE_II; mac = false; poll_media(); assert(events == 3 && !wp_seen && wp_reads == reads);
    /* back to drive B with the other state: baseline, no event */
    cur = DRIVE_SHUGART_B; poll_media(); assert(events == 3 && wp_seen == 1);

    /* the Mac drive on the Apple port does report */
    cur = DRIVE_APPLE2; mac = true; wp = true; poll_media(); assert(events == 3 && wp_seen == 2);
    wp = false; poll_media(); assert(events == 4 && last[0] == DRIVE_APPLE2 && last[1] == 0);

    /* EVENTS resets the baseline (wp_seen = 0): no event for a change across it */
    wp_seen = 0; wp = true; poll_media(); assert(events == 4);

    /* disk change keeps its behaviour: rising edge only, not without a drive */
    ev_mask = 1u; mac = false; cur = DRIVE_SHUGART_A; dc = true; events = 0;
    poll_media(); assert(events == 1 && last_code == UFI_V2_EVT_DISK && last_len == 1 && last[0] == DRIVE_SHUGART_A);
    poll_media(); assert(events == 1);
    dc = false; poll_media(); dc = true; poll_media(); assert(events == 2);
    cur = DRIVE_NONE; last_dc = false; poll_media(); assert(events == 2);

    /* both bits: a protected disk taken out reports both */
    ev_mask = 1u | WP; cur = DRIVE_SHUGART_A; dc = false; wp = true; last_dc = false; wp_seen = 0; events = 0;
    poll_media(); assert(!events);
    dc = true; wp = false; poll_media(); assert(events == 2);
    return 0;
}
'''
    run('regression_event_poll', code)
    queue()
    board_rev()


def board_rev():
    """ufi_board_rev(): one ADC conversion, never one in an interrupt.  Until 2026-11-14 every
    call converted, also from the write-path interrupt handlers."""
    import re
    from test_regressions import FW
    board = (FW / 'include' / 'board.h').read_text(encoding='utf-8')
    mv = {n: int(re.search(r'#define BOARD_ID_%s_MV\s+(\d+)u' % n, board).group(1)) for n in ('V05', 'V06', 'V07', 'V10')}
    code = r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#define ADC_CH_BOARD_ID 7u
#define BOARD_ID_V05_MV %du
#define BOARD_ID_V06_MV %du
#define BOARD_ID_V07_MV %du
#define BOARD_ID_V10_MV %du
static unsigned ipsr, conversions;
static uint16_t level;
static unsigned __get_IPSR(void) { return ipsr; }
static uint16_t ufi_adc_mv(uint32_t ch) { assert(ch == ADC_CH_BOARD_ID && !ipsr); conversions++; return level; }
''' % (mv['V05'], mv['V06'], mv['V07'], mv['V10']) + function('ufi_v2.c', 'ufi_board_rev') + r'''
int main(void)
{
    /* in an interrupt before anything is known: no conversion, revision unknown */
    ipsr = 44; level = BOARD_ID_V10_MV;
    assert(ufi_board_rev() == 0 && !conversions);
    /* ADC not ready yet (0 mV): unknown, and measured again on the next thread call */
    ipsr = 0; level = 0;
    assert(ufi_board_rev() == 0 && conversions == 1);
    level = BOARD_ID_V10_MV + 150u;
    assert(ufi_board_rev() == 10 && conversions == 2);
    /* recognised: final.  No further conversion, whatever the ADC would say now, in any context */
    level = BOARD_ID_V07_MV;
    for (unsigned i = 0; i < 5; i++) assert(ufi_board_rev() == 10);
    ipsr = 44; assert(ufi_board_rev() == 10);
    assert(conversions == 2);
    return 0;
}
'''
    run('regression_board_rev', code)


def queue():
    """ufi_v2_event(): every event the firmware produces must fit the queue.  Until 2026-11-14
    the limit was 8 bytes and the 9-byte read progress event 0x85 was dropped without a trace."""
    import re
    from test_regressions import FW
    header = (FW / 'include' / 'ufi_firmware.h').read_text(encoding='utf-8')
    source = (FW / 'src' / 'ufi_v2.c').read_text(encoding='utf-8')
    limit = int(re.search(r'#define UFI_V2_EVENT_MAX (\d+)u', header).group(1))
    progress = int(re.search(r'#define UFI_V2_EVT_READ_PROGRESS_SIZE (\d+)u', header).group(1))
    size = int(re.search(r'#define EVQ_SIZE\s+(\d+)u', source).group(1))
    assert re.search(r'#define EVQ_PAYLOAD\s+UFI_V2_EVENT_MAX\b', source), 'queue limit no longer the shared constant'
    code = r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#define EVQ_PAYLOAD %du
#define EVQ_SIZE %du
#define PROGRESS %du
enum { UFI_V2_EVT_DISK = 0x80, UFI_V2_EVT_READ_PROGRESS = 0x85, UFI_ROUTE_USB = 1, UFI_ROUTE_LAN = 2 };
typedef struct { uint8_t code, len; uint8_t data[EVQ_PAYLOAD]; } v2_event_t;
static v2_event_t evq[EVQ_SIZE];
static uint8_t ev_head, ev_tail;
static uint32_t ev_mask, ev_dropped_full, ev_dropped_size;
static bool on, lan_up;
static struct { uint8_t ev_route; } link;
static bool ufi_lan_connected(void) { return lan_up; }
''' % (limit, size, progress) + function('ufi_v2.c', 'ufi_v2_event') + r'''
int main(void)
{
    uint8_t p[EVQ_PAYLOAD + 4];
    for (unsigned i = 0; i < sizeof p; i++) p[i] = (uint8_t)(0xA0 + i);
    on = true; ev_mask = 1u << 5;
    assert(PROGRESS <= EVQ_PAYLOAD);
    ufi_v2_event(UFI_V2_EVT_READ_PROGRESS, p, PROGRESS);            /* the 9-byte progress event is queued whole */
    assert(ev_head == 1 && evq[0].code == 0x85 && evq[0].len == PROGRESS && !memcmp(evq[0].data, p, PROGRESS));
    ufi_v2_event(UFI_V2_EVT_READ_PROGRESS, p, EVQ_PAYLOAD);         /* the limit itself fits */
    assert(ev_head == 2 && evq[1].len == EVQ_PAYLOAD);
    ufi_v2_event(UFI_V2_EVT_READ_PROGRESS, p, EVQ_PAYLOAD + 1);     /* one more does not, and writes nothing */
    assert(ev_head == 2 && ev_dropped_size == 1 && !ev_dropped_full);   /* ... but is counted with its cause */
    ufi_v2_event(UFI_V2_EVT_DISK, p, 1); assert(ev_head == 2);      /* mask bit clear */
    ufi_v2_event(0x7F, p, 1); ufi_v2_event(0xA0, p, 1); assert(ev_head == 2);   /* outside the 32-bit mask */
    assert(ev_dropped_size == 1 && !ev_dropped_full);               /* not asked for: no loss */
    ev_mask = 0xFFFFFFFFu;
    for (unsigned i = 0; i < 2u * EVQ_SIZE; i++) ufi_v2_event(UFI_V2_EVT_DISK, p, 1);
    assert((uint8_t)((ev_head + 1u) % EVQ_SIZE) == ev_tail);        /* full: one slot stays free, the rest dropped */
    assert(ev_dropped_full == 2u * EVQ_SIZE - (EVQ_SIZE - 3u) && ev_dropped_size == 1);   /* 2 were queued before: one more fit */
    ev_head = ev_tail = 0; on = false;
    ufi_v2_event(UFI_V2_EVT_DISK, p, 1); assert(!ev_head && ev_dropped_size == 1);   /* no session: no loss either */
    link.ev_route = UFI_ROUTE_LAN; lan_up = true;
    ufi_v2_event(UFI_V2_EVT_DISK, p, 1); assert(ev_head == 1);      /* LAN session counts by its connection */
    return 0;
}
'''
    run('regression_event_queue', code)


if __name__ == '__main__':
    main()
