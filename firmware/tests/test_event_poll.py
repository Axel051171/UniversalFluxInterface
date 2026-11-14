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


if __name__ == '__main__':
    main()
