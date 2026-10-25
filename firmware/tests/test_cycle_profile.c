/* Real clock adapter with controlled DWT/SysTick registers; no hardware timings. */
#include "check.h"
static struct {unsigned CTRL,CYCCNT;} dwt;
#define DWT (&dwt)
#define DWT_CTRL_CYCCNTENA_Msk 1u
#define UFI_PROFILE_TEST_HARDWARE 1
static unsigned SystemCoreClock=550000000,tick;
static unsigned HAL_GetTick(void){return tick;}
#include "ufi_profile.h"
#include "../src/ufi_metrics.c"
int main(void){
    ufi_cycle_snapshot_t report;
    dwt.CTRL=1;dwt.CYCCNT=UINT32_MAX-9;tick=UINT32_MAX-3;
    ufi_cycles_init(SystemCoreClock,true);
    CHECK(dwt.CYCCNT==UINT32_MAX-9,"metrics init does not reset DWT delays");
    ufi_profile_mark_t mark=ufi_profile_begin();dwt.CYCCNT=10;tick=2;
    ufi_profile_end(UFI_CYCLE_MAIN,mark);ufi_cycles_snapshot(&report);
    CHECK(report.channel[0].calls==1 && report.channel[0].maximum==20,"DWT and SysTick wraps");
    mark=ufi_profile_begin();SystemCoreClock=480000000;dwt.CYCCNT+=500;
    ufi_profile_end(UFI_CYCLE_MAIN,mark);ufi_cycles_snapshot(&report);
    CHECK(report.channel[0].calls==1 && (report.flags&UFI_CYCLE_CLOCK_CHANGED),"clock changes excluded");
    SystemCoreClock=550000000;mark=ufi_profile_begin();dwt.CTRL=0;
    ufi_profile_end(UFI_CYCLE_CAPTURE_DMA,mark);ufi_cycles_snapshot(&report);
    CHECK(!(report.flags&1) && (report.flags&UFI_CYCLE_UNAVAILABLE),"stopped counter flagged");
    dwt.CTRL=1;ufi_cycles_init(SystemCoreClock,true);
    mark=ufi_profile_begin();tick+=7808;dwt.CYCCNT++;
    ufi_profile_end(UFI_CYCLE_DECODER,mark);ufi_cycles_snapshot(&report);
    CHECK(!report.channel[4].calls && (report.flags&UFI_CYCLE_INVALID_SPAN),"ambiguous long span excluded");
    cycles.channel[UFI_CYCLE_WRITE_DMA].calls=UINT32_MAX-1;
    mark=ufi_profile_begin();dwt.CYCCNT+=100;
    ufi_profile_end(UFI_CYCLE_WRITE_DMA,mark);ufi_profile_end(UFI_CYCLE_WRITE_DMA,mark);
    ufi_cycles_snapshot(&report);
    CHECK(report.channel[2].calls==UINT32_MAX && (report.flags&UFI_CYCLE_SATURATED),"cycle count saturates");
    CHECK(report.flags&UFI_CYCLE_INVALID_SPAN,"independent channel flags retained");
    mark=ufi_profile_begin();ufi_profile_end(UFI_CYCLE_VERIFY,mark);ufi_cycles_snapshot(&report);
    CHECK(!report.channel[5].calls && (report.flags&UFI_CYCLE_UNAVAILABLE),"frozen DWT excluded despite enabled control bit");
    uint8_t out[64];CHECK(ufi_cycles_pack(&report,out,sizeof out)==64 && out[3]==report.flags,"flags reach wire");
    CHECK_SUMMARY("test_cycle_profile");
}
