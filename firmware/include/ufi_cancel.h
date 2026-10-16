#pragma once
#include <stdbool.h>
void ufi_cancel_request(void);
bool ufi_cancel_requested(void);
void ufi_cancel_clear(void);
void ufi_delay_us(unsigned us);
bool ufi_delay_ms(unsigned ms);
