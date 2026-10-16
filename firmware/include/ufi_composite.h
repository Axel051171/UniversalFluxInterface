#pragma once
#include "usbd_def.h"
#include <stdbool.h>
extern USBD_ClassTypeDef UFI_Composite;
bool ufi_native_tx_idle(void);
int ufi_native_tx_start(const uint8_t *data, uint32_t len);
/* Historical name: Composite copies into a bounded queue (accepted, not yet
 * delivered); the standalone CDC personality still waits for completion. */
int ufi_native_tx_blocking(const uint8_t *data, uint32_t len);
void ufi_native_rx_resume(void);
void ufi_composite_disconnect(void);
void ufi_composite_service(void);
bool ufi_native_tx_room(void);
void ufi_native_tx_service(void);
bool ufi_native_tx_flush(uint32_t timeout_ms);
uint32_t ufi_native_tx_timeouts(void);
