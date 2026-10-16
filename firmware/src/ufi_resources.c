#include "ufi_resources.h"
static uint32_t native_activity;
/* Main-loop only. Retain drive/timer/store bundle until explicit release. */
static volatile uint8_t current; /* read by endpoint-zero cancellation callback */
bool ufi_resources_acquire(uint8_t owner)
{
    if (!owner || owner > UFI_OWNER_GW || (current && current != owner)) return false;
    current = owner;
    return true;
}
bool ufi_resources_release(uint8_t owner)
{
    if (!owner || current != owner) return false;
    current = UFI_OWNER_NONE;
    return true;
}
uint8_t ufi_resources_owner(void) { return current; }
void ufi_resources_reset(void) { current = UFI_OWNER_NONE; }
void ufi_resources_touch(uint32_t now) {native_activity=now;}
bool ufi_resources_idle_expired(uint32_t now, bool busy)
{
    if(current!=UFI_OWNER_NATIVE)return false;
    if(busy) {native_activity=now;return false;}
    return now-native_activity>=30000u;
}
