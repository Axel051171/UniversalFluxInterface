#include "ufi_firmware.h"
#include "ufi_cancel.h"
static volatile bool cancelled;
void ufi_cancel_request(void) {cancelled=true;}
bool ufi_cancel_requested(void) {return cancelled;}
void ufi_cancel_clear(void) {cancelled=false;}
void ufi_delay_us(unsigned us)
{
    const uint64_t end=ufi_flux_now64()+(uint64_t)us*(FLUX_TIMER_FREQ/1000000u);
    while(ufi_flux_now64()<end && !cancelled) {UFI_WATCHDOG_FEED();}
}
bool ufi_delay_ms(unsigned ms)
{
    const uint32_t start=HAL_GetTick();
    while(HAL_GetTick()-start<ms) {
        if(cancelled)return false;
        UFI_WATCHDOG_FEED();
    }
    return !cancelled;
}
