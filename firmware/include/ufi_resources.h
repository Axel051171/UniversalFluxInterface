#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { UFI_OWNER_NONE, UFI_OWNER_NATIVE, UFI_OWNER_GW };
bool ufi_resources_acquire(uint8_t owner);
bool ufi_resources_release(uint8_t owner);
uint8_t ufi_resources_owner(void);
void ufi_resources_reset(void);
void ufi_resources_touch(uint32_t now);
bool ufi_resources_idle_expired(uint32_t now, bool busy);
