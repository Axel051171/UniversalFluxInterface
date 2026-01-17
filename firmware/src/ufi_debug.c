/**
 * UFI Flux Engine - Debug Commands
 * 
 * Hardware-Test und Diagnose-Funktionen
 */

#include "ufi_firmware.h"
#include "stm32h7xx_hal.h"

/* ============================================================================
 * GPIO PINS (Referenzen aus ufi_main.c)
 * ============================================================================ */

extern const gpio_pin_t PIN_FDD_MOTOR_A;
extern const gpio_pin_t PIN_FDD_MOTOR_B;
extern const gpio_pin_t PIN_FDD_DRV_SEL_A;
extern const gpio_pin_t PIN_FDD_DRV_SEL_B;
extern const gpio_pin_t PIN_FDD_STEP;
extern const gpio_pin_t PIN_FDD_DIR;
extern const gpio_pin_t PIN_FDD_SIDE_SEL;
extern const gpio_pin_t PIN_FDD_WGATE;
extern const gpio_pin_t PIN_FDD_WDATA;

extern const gpio_pin_t PIN_FDD_INDEX;
extern const gpio_pin_t PIN_FDD_TRACK0;
extern const gpio_pin_t PIN_FDD_WPROT;
extern const gpio_pin_t PIN_FDD_RDATA;
extern const gpio_pin_t PIN_FDD_DKCHG;

extern const gpio_pin_t PIN_IEC_ATN;
extern const gpio_pin_t PIN_IEC_CLK;
extern const gpio_pin_t PIN_IEC_DATA;
extern const gpio_pin_t PIN_IEC_SRQ;
extern const gpio_pin_t PIN_IEC_RESET;

extern const gpio_pin_t PIN_LED_PWR;
extern const gpio_pin_t PIN_LED_ACT;
extern const gpio_pin_t PIN_LED_FDD;
extern const gpio_pin_t PIN_LED_USB;

extern TIM_HandleTypeDef htim2;

/* ============================================================================
 * DEBUG GPIO
 * ============================================================================ */

/**
 * GPIO Status lesen
 * Gibt Bitmask aller Input-Signale zurück
 */
typedef struct __packed {
    uint16_t fdd_inputs;    // Bit0=INDEX, Bit1=TRK0, Bit2=WPROT, Bit3=RDATA, Bit4=DKCHG
    uint16_t fdd_outputs;   // Bit0=MOTOR_A, Bit1=MOTOR_B, Bit2=SEL_A, Bit3=SEL_B, etc.
    uint16_t iec_signals;   // Bit0=ATN, Bit1=CLK, Bit2=DATA, Bit3=SRQ, Bit4=RESET
    uint16_t leds;          // Bit0=PWR, Bit1=ACT, Bit2=FDD, Bit3=USB
} gpio_status_t;

gpio_status_t ufi_debug_gpio_read(void) {
    gpio_status_t status = {0};
    
    // FDD Inputs (active low, invertiert für logischen Wert)
    if (HAL_GPIO_ReadPin(PIN_FDD_INDEX.port, PIN_FDD_INDEX.pin) == GPIO_PIN_RESET)
        status.fdd_inputs |= (1 << 0);
    if (HAL_GPIO_ReadPin(PIN_FDD_TRACK0.port, PIN_FDD_TRACK0.pin) == GPIO_PIN_RESET)
        status.fdd_inputs |= (1 << 1);
    if (HAL_GPIO_ReadPin(PIN_FDD_WPROT.port, PIN_FDD_WPROT.pin) == GPIO_PIN_RESET)
        status.fdd_inputs |= (1 << 2);
    if (HAL_GPIO_ReadPin(PIN_FDD_RDATA.port, PIN_FDD_RDATA.pin) == GPIO_PIN_RESET)
        status.fdd_inputs |= (1 << 3);
    if (HAL_GPIO_ReadPin(PIN_FDD_DKCHG.port, PIN_FDD_DKCHG.pin) == GPIO_PIN_RESET)
        status.fdd_inputs |= (1 << 4);
    
    // FDD Outputs (aktueller Zustand)
    if (HAL_GPIO_ReadPin(PIN_FDD_MOTOR_A.port, PIN_FDD_MOTOR_A.pin) == GPIO_PIN_RESET)
        status.fdd_outputs |= (1 << 0);
    if (HAL_GPIO_ReadPin(PIN_FDD_MOTOR_B.port, PIN_FDD_MOTOR_B.pin) == GPIO_PIN_RESET)
        status.fdd_outputs |= (1 << 1);
    if (HAL_GPIO_ReadPin(PIN_FDD_DRV_SEL_A.port, PIN_FDD_DRV_SEL_A.pin) == GPIO_PIN_RESET)
        status.fdd_outputs |= (1 << 2);
    if (HAL_GPIO_ReadPin(PIN_FDD_DRV_SEL_B.port, PIN_FDD_DRV_SEL_B.pin) == GPIO_PIN_RESET)
        status.fdd_outputs |= (1 << 3);
    if (HAL_GPIO_ReadPin(PIN_FDD_STEP.port, PIN_FDD_STEP.pin) == GPIO_PIN_RESET)
        status.fdd_outputs |= (1 << 4);
    if (HAL_GPIO_ReadPin(PIN_FDD_DIR.port, PIN_FDD_DIR.pin) == GPIO_PIN_RESET)
        status.fdd_outputs |= (1 << 5);
    if (HAL_GPIO_ReadPin(PIN_FDD_SIDE_SEL.port, PIN_FDD_SIDE_SEL.pin) == GPIO_PIN_RESET)
        status.fdd_outputs |= (1 << 6);
    if (HAL_GPIO_ReadPin(PIN_FDD_WGATE.port, PIN_FDD_WGATE.pin) == GPIO_PIN_RESET)
        status.fdd_outputs |= (1 << 7);
    if (HAL_GPIO_ReadPin(PIN_FDD_WDATA.port, PIN_FDD_WDATA.pin) == GPIO_PIN_RESET)
        status.fdd_outputs |= (1 << 8);
    
    // IEC Bus (active low)
    if (HAL_GPIO_ReadPin(PIN_IEC_ATN.port, PIN_IEC_ATN.pin) == GPIO_PIN_RESET)
        status.iec_signals |= (1 << 0);
    if (HAL_GPIO_ReadPin(PIN_IEC_CLK.port, PIN_IEC_CLK.pin) == GPIO_PIN_RESET)
        status.iec_signals |= (1 << 1);
    if (HAL_GPIO_ReadPin(PIN_IEC_DATA.port, PIN_IEC_DATA.pin) == GPIO_PIN_RESET)
        status.iec_signals |= (1 << 2);
    if (HAL_GPIO_ReadPin(PIN_IEC_SRQ.port, PIN_IEC_SRQ.pin) == GPIO_PIN_RESET)
        status.iec_signals |= (1 << 3);
    if (HAL_GPIO_ReadPin(PIN_IEC_RESET.port, PIN_IEC_RESET.pin) == GPIO_PIN_RESET)
        status.iec_signals |= (1 << 4);
    
    // LEDs (active high)
    if (HAL_GPIO_ReadPin(PIN_LED_PWR.port, PIN_LED_PWR.pin) == GPIO_PIN_SET)
        status.leds |= (1 << 0);
    if (HAL_GPIO_ReadPin(PIN_LED_ACT.port, PIN_LED_ACT.pin) == GPIO_PIN_SET)
        status.leds |= (1 << 1);
    if (HAL_GPIO_ReadPin(PIN_LED_FDD.port, PIN_LED_FDD.pin) == GPIO_PIN_SET)
        status.leds |= (1 << 2);
    if (HAL_GPIO_ReadPin(PIN_LED_USB.port, PIN_LED_USB.pin) == GPIO_PIN_SET)
        status.leds |= (1 << 3);
    
    return status;
}

/**
 * Einzelnes GPIO setzen (für Tests)
 * @param gpio_id   0-8 = FDD Outputs, 16-20 = IEC, 32-35 = LEDs
 * @param state     0 = Inaktiv, 1 = Aktiv
 */
int ufi_debug_gpio_set(uint8_t gpio_id, uint8_t state) {
    GPIO_TypeDef* port = NULL;
    uint16_t pin = 0;
    bool active_low = true;
    
    switch (gpio_id) {
        // FDD Outputs
        case 0:  port = PIN_FDD_MOTOR_A.port;   pin = PIN_FDD_MOTOR_A.pin;   break;
        case 1:  port = PIN_FDD_MOTOR_B.port;   pin = PIN_FDD_MOTOR_B.pin;   break;
        case 2:  port = PIN_FDD_DRV_SEL_A.port; pin = PIN_FDD_DRV_SEL_A.pin; break;
        case 3:  port = PIN_FDD_DRV_SEL_B.port; pin = PIN_FDD_DRV_SEL_B.pin; break;
        case 4:  port = PIN_FDD_STEP.port;      pin = PIN_FDD_STEP.pin;      break;
        case 5:  port = PIN_FDD_DIR.port;       pin = PIN_FDD_DIR.pin;       break;
        case 6:  port = PIN_FDD_SIDE_SEL.port;  pin = PIN_FDD_SIDE_SEL.pin;  break;
        case 7:  port = PIN_FDD_WGATE.port;     pin = PIN_FDD_WGATE.pin;     break;
        case 8:  port = PIN_FDD_WDATA.port;     pin = PIN_FDD_WDATA.pin;     break;
        
        // IEC Bus
        case 16: port = PIN_IEC_ATN.port;   pin = PIN_IEC_ATN.pin;   break;
        case 17: port = PIN_IEC_CLK.port;   pin = PIN_IEC_CLK.pin;   break;
        case 18: port = PIN_IEC_DATA.port;  pin = PIN_IEC_DATA.pin;  break;
        case 19: port = PIN_IEC_SRQ.port;   pin = PIN_IEC_SRQ.pin;   break;
        case 20: port = PIN_IEC_RESET.port; pin = PIN_IEC_RESET.pin; break;
        
        // LEDs (active high)
        case 32: port = PIN_LED_PWR.port; pin = PIN_LED_PWR.pin; active_low = false; break;
        case 33: port = PIN_LED_ACT.port; pin = PIN_LED_ACT.pin; active_low = false; break;
        case 34: port = PIN_LED_FDD.port; pin = PIN_LED_FDD.pin; active_low = false; break;
        case 35: port = PIN_LED_USB.port; pin = PIN_LED_USB.pin; active_low = false; break;
        
        default:
            return UFI_ERR_NOT_IMPL;
    }
    
    if (active_low) {
        HAL_GPIO_WritePin(port, pin, state ? GPIO_PIN_RESET : GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(port, pin, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
    
    return UFI_OK;
}

/* ============================================================================
 * DEBUG TIMER
 * ============================================================================ */

/**
 * Timer Status lesen
 */
typedef struct __packed {
    uint32_t tim2_counter;      // Aktueller TIM2 Zählerwert
    uint32_t tim2_prescaler;    // TIM2 Prescaler
    uint32_t tim2_period;       // TIM2 Period
    uint32_t sysclk_freq;       // System Clock Frequenz
    uint32_t hclk_freq;         // AHB Clock
    uint32_t pclk1_freq;        // APB1 Clock (Timer)
    uint32_t uptime_ms;         // Uptime in ms
} timer_status_t;

timer_status_t ufi_debug_timer_read(void) {
    timer_status_t status = {0};
    
    status.tim2_counter = __HAL_TIM_GET_COUNTER(&htim2);
    status.tim2_prescaler = htim2.Instance->PSC;
    status.tim2_period = htim2.Instance->ARR;
    status.sysclk_freq = HAL_RCC_GetSysClockFreq();
    status.hclk_freq = HAL_RCC_GetHCLKFreq();
    status.pclk1_freq = HAL_RCC_GetPCLK1Freq();
    status.uptime_ms = HAL_GetTick();
    
    return status;
}

/**
 * Index-Puls Timing messen
 * Misst Zeit zwischen zwei Index-Pulsen (= 1 Umdrehung)
 * @return Zeit in Timer-Ticks, 0 bei Timeout
 */
uint32_t ufi_debug_measure_index(void) {
    uint32_t timeout = HAL_GetTick() + 1000;  // 1s Timeout
    
    // Warte auf erste fallende Flanke
    while (HAL_GPIO_ReadPin(PIN_FDD_INDEX.port, PIN_FDD_INDEX.pin) == GPIO_PIN_RESET) {
        if (HAL_GetTick() > timeout) return 0;
    }
    while (HAL_GPIO_ReadPin(PIN_FDD_INDEX.port, PIN_FDD_INDEX.pin) == GPIO_PIN_SET) {
        if (HAL_GetTick() > timeout) return 0;
    }
    
    // Erste Flanke - Timer starten
    uint32_t start = __HAL_TIM_GET_COUNTER(&htim2);
    
    // Warte auf zweite fallende Flanke
    while (HAL_GPIO_ReadPin(PIN_FDD_INDEX.port, PIN_FDD_INDEX.pin) == GPIO_PIN_RESET) {
        if (HAL_GetTick() > timeout) return 0;
    }
    while (HAL_GPIO_ReadPin(PIN_FDD_INDEX.port, PIN_FDD_INDEX.pin) == GPIO_PIN_SET) {
        if (HAL_GetTick() > timeout) return 0;
    }
    
    uint32_t end = __HAL_TIM_GET_COUNTER(&htim2);
    
    return end - start;
}

/**
 * RPM aus Index-Timing berechnen
 */
uint16_t ufi_debug_measure_rpm(void) {
    uint32_t ticks = ufi_debug_measure_index();
    if (ticks == 0) return 0;
    
    // RPM = 60 / (ticks * tick_period)
    // Bei 275 MHz: tick_period = 3.636 ns
    // RPM = 60 / (ticks * 3.636e-9) = 60e9 / (ticks * 3.636)
    // RPM ≈ 16.5e9 / ticks
    
    uint64_t rpm = (60ULL * FLUX_TIMER_FREQ) / ticks;
    return (uint16_t)rpm;
}

/* ============================================================================
 * DEBUG MEMORY
 * ============================================================================ */

/**
 * Memory-Info lesen
 */
typedef struct __packed {
    uint32_t flash_size;        // Flash Größe in KB
    uint32_t ram_size;          // RAM Größe (DTCM + SRAM)
    uint32_t unique_id[3];      // 96-bit Unique ID
    uint16_t revision;          // Device Revision
    uint16_t device_id;         // Device ID
} memory_info_t;

memory_info_t ufi_debug_memory_read(void) {
    memory_info_t info = {0};
    
    // Flash Size (in KB) @ 0x1FF1E880
    info.flash_size = *(uint16_t*)0x1FF1E880;
    
    // Unique ID @ UID_BASE (0x1FF1E800)
    info.unique_id[0] = *(uint32_t*)(UID_BASE);
    info.unique_id[1] = *(uint32_t*)(UID_BASE + 4);
    info.unique_id[2] = *(uint32_t*)(UID_BASE + 8);
    
    // Device ID und Revision @ DBGMCU_IDCODE (0xE00E1000)
    uint32_t idcode = DBGMCU->IDCODE;
    info.device_id = idcode & 0xFFF;
    info.revision = (idcode >> 16) & 0xFFFF;
    
    // RAM Size (fix für STM32H723)
    info.ram_size = 564;  // 564 KB total
    
    return info;
}

/* ============================================================================
 * LED TEST
 * ============================================================================ */

/**
 * LED Sequenz für visuellen Test
 */
void ufi_debug_led_test(void) {
    // Alle LEDs aus
    HAL_GPIO_WritePin(PIN_LED_PWR.port, PIN_LED_PWR.pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_LED_ACT.port, PIN_LED_ACT.pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_LED_FDD.port, PIN_LED_FDD.pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_LED_USB.port, PIN_LED_USB.pin, GPIO_PIN_RESET);
    HAL_Delay(200);
    
    // Sequenz: PWR -> ACT -> FDD -> USB -> Alle
    HAL_GPIO_WritePin(PIN_LED_PWR.port, PIN_LED_PWR.pin, GPIO_PIN_SET);
    HAL_Delay(200);
    HAL_GPIO_WritePin(PIN_LED_ACT.port, PIN_LED_ACT.pin, GPIO_PIN_SET);
    HAL_Delay(200);
    HAL_GPIO_WritePin(PIN_LED_FDD.port, PIN_LED_FDD.pin, GPIO_PIN_SET);
    HAL_Delay(200);
    HAL_GPIO_WritePin(PIN_LED_USB.port, PIN_LED_USB.pin, GPIO_PIN_SET);
    HAL_Delay(500);
    
    // Alle aus, nur PWR an (Normalzustand)
    HAL_GPIO_WritePin(PIN_LED_ACT.port, PIN_LED_ACT.pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_LED_FDD.port, PIN_LED_FDD.pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_LED_USB.port, PIN_LED_USB.pin, GPIO_PIN_RESET);
}

/* ============================================================================
 * SELBSTTEST
 * ============================================================================ */

/**
 * Hardware-Selbsttest
 * @return Bitmask: Bit0=Timer OK, Bit1=DMA OK, Bit2=USB OK, Bit3=GPIO OK
 */
uint8_t ufi_debug_selftest(void) {
    uint8_t result = 0;
    
    // Timer Test: TIM2 läuft?
    uint32_t t1 = __HAL_TIM_GET_COUNTER(&htim2);
    HAL_Delay(1);
    uint32_t t2 = __HAL_TIM_GET_COUNTER(&htim2);
    if (t2 > t1) {
        result |= (1 << 0);  // Timer OK
    }
    
    // DMA Test: DMA1 Clock enabled?
    if (__HAL_RCC_DMA1_IS_CLK_ENABLED()) {
        result |= (1 << 1);  // DMA OK
    }
    
    // USB Test: USB Clock enabled?
    if (__HAL_RCC_USB_OTG_HS_IS_CLK_ENABLED()) {
        result |= (1 << 2);  // USB OK
    }
    
    // GPIO Test: Alle Ports aktiviert?
    if (__HAL_RCC_GPIOA_IS_CLK_ENABLED() &&
        __HAL_RCC_GPIOB_IS_CLK_ENABLED() &&
        __HAL_RCC_GPIOC_IS_CLK_ENABLED() &&
        __HAL_RCC_GPIOD_IS_CLK_ENABLED() &&
        __HAL_RCC_GPIOE_IS_CLK_ENABLED()) {
        result |= (1 << 3);  // GPIO OK
    }
    
    return result;
}
