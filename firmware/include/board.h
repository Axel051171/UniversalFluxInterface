/**
 * UFI Flux Engine - Board Definition: UFI Headless (STM32H723ZGT6, LQFP144)
 *
 * Single source of truth for pin assignment and signal polarity.
 * Schematic: kicad/UFI_Headless (README "GPIO-Belegung").
 *
 * Polarity on this board:
 *  - Outputs (FDD_*, IEC_*_OUT) drive SN74LS07 open-collector buffers:
 *    MCU low  = bus line asserted (low), MCU high = released.
 *  - Inputs (FDD_INDEX/TRK0/WPROT/RDATA/DSKCHG/READY, IEC_*_IN) pass through
 *    74LVC14A Schmitt inverters: MCU high = bus line asserted (low).
 *    Flux/index capture therefore triggers on the RISING edge.
 */

#ifndef BOARD_H
#define BOARD_H

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define BOARD_NAME              "UFI Headless"
#define BOARD_HAS_APPLE         0       /* no Apple Disk II port on this board */

typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
} gpio_pin_t;

/* ---- FDD outputs (34-pin J6 + Amiga J7), GPIOE ------------------------- */
extern const gpio_pin_t PIN_FDD_MOTOR_A;    /* PE7  */
extern const gpio_pin_t PIN_FDD_MOTOR_B;    /* PE8  (Amiga MTRXD) */
extern const gpio_pin_t PIN_FDD_DRV_SEL_A;  /* PE9  */
extern const gpio_pin_t PIN_FDD_DRV_SEL_B;  /* PE10 (Amiga SEL1B) */
extern const gpio_pin_t PIN_FDD_STEP;       /* PE11 */
extern const gpio_pin_t PIN_FDD_DIR;        /* PE12 */
extern const gpio_pin_t PIN_FDD_SIDE_SEL;   /* PE13 */
extern const gpio_pin_t PIN_FDD_WGATE;      /* PE14 */
extern const gpio_pin_t PIN_FDD_DENSITY;    /* PE15 */
extern const gpio_pin_t PIN_FDD_DRATE;      /* PF11, J6 pin 6 via solder jumper JP1 (v0.4) */
extern const gpio_pin_t PIN_FDD_WDATA;      /* PA6, TIM3_CH1 (AF2) write pulses */

/* ---- FDD inputs ---------------------------------------------------------- */
extern const gpio_pin_t PIN_FDD_RDATA;      /* PA5, TIM2_CH1 (AF1) input capture */
extern const gpio_pin_t PIN_FDD_INDEX;      /* PA1, TIM2_CH2 (AF1) input capture */
extern const gpio_pin_t PIN_FDD_TRACK0;     /* PF0 */
extern const gpio_pin_t PIN_FDD_WPROT;      /* PF1 */
extern const gpio_pin_t PIN_FDD_DKCHG;      /* PF2 */
extern const gpio_pin_t PIN_FDD_READY;      /* PF3 */

#define FDD_RDATA_AF            GPIO_AF1_TIM2
#define FDD_INDEX_AF            GPIO_AF1_TIM2
#define FDD_WDATA_AF            GPIO_AF2_TIM3   /* TIM3_CH1 */
#define FLUX_RDATA_CHANNEL      TIM_CHANNEL_1
#define FLUX_INDEX_CHANNEL      TIM_CHANNEL_2

/* ---- IEC bus: separate driver outputs and receiver inputs ---------------- */
extern const gpio_pin_t PIN_IEC_ATN_OUT;    /* PD0 */
extern const gpio_pin_t PIN_IEC_CLK_OUT;    /* PD1 */
extern const gpio_pin_t PIN_IEC_DATA_OUT;   /* PD2 */
extern const gpio_pin_t PIN_IEC_SRQ_OUT;    /* PD3 */
extern const gpio_pin_t PIN_IEC_RESET_OUT;  /* PD4 */
extern const gpio_pin_t PIN_IEC_ATN_IN;     /* PF4 */
extern const gpio_pin_t PIN_IEC_CLK_IN;     /* PF5 */
extern const gpio_pin_t PIN_IEC_DATA_IN;    /* PF6 */
extern const gpio_pin_t PIN_IEC_SRQ_IN;     /* PF7 */
extern const gpio_pin_t PIN_IEC_RESET_IN;   /* PF8 */

/* ---- Status, LEDs (high = on; power LED is hard-wired to 3V3) ------------ */
extern const gpio_pin_t PIN_LED_ACT;        /* PG0 */
extern const gpio_pin_t PIN_LED_FDD;        /* PG1 */
extern const gpio_pin_t PIN_LED_USB;        /* PG2 */
extern const gpio_pin_t PIN_LED_ERR;        /* PG3 */
extern const gpio_pin_t PIN_PWR_SRC;        /* PG4, TPS2116 status input */
extern const gpio_pin_t PIN_VBUS_SENSE;     /* PA9, 22k/33k divider */

/* v0.5: drive supply switches (high = on, 100k pull-downs keep them off in reset),
 * write lock jumper, buttons, expansion GPIO, microSD card detect */
extern const gpio_pin_t PIN_FDD5_EN;        /* PE2 */
extern const gpio_pin_t PIN_FDD12_EN;       /* PE3 */
extern const gpio_pin_t PIN_WLOCK;          /* PE4, high = WRITE LOCK jumper set */
extern const gpio_pin_t PIN_BTN_A;          /* PB8, low = pressed */
extern const gpio_pin_t PIN_BTN_B;          /* PB9, low = pressed */
extern const gpio_pin_t PIN_SD_CD;          /* PG13, low = card inserted (switch to GND) */
#define ADC_CH_I_FDD5           ADC_CHANNEL_10  /* PC0, INA180A1: 2 V/A */
#define ADC_CH_I_FDD12          ADC_CHANNEL_11  /* PC1 */
#define ADC_CH_BOARD_ID         ADC_CHANNEL_18  /* PA4, 10k/10k = 1650 mV = v0.5 */
#define BOARD_ID_V05_MV         1650u

/* ---- QSPI PSRAM (board v0.2): APS6404L 8 MB on OCTOSPIM port 1 ---------- */
#define BOARD_HAS_PSRAM         1
#define PSRAM_SIZE_BYTES        (8u * 1024u * 1024u)
#define PSRAM_CLK_PORT  GPIOB
#define PSRAM_CLK_PIN   GPIO_PIN_2      /* AF9 */
#define PSRAM_NCS_PORT  GPIOB
#define PSRAM_NCS_PIN   GPIO_PIN_10     /* AF9 */
#define PSRAM_IO0_PORT  GPIOD
#define PSRAM_IO0_PIN   GPIO_PIN_11     /* AF9 */
#define PSRAM_IO1_PORT  GPIOD
#define PSRAM_IO1_PIN   GPIO_PIN_12     /* AF9 */
#define PSRAM_IO2_PORT  GPIOB
#define PSRAM_IO2_PIN   GPIO_PIN_13     /* AF4 */
#define PSRAM_IO3_PORT  GPIOD
#define PSRAM_IO3_PIN   GPIO_PIN_13     /* AF9 */

/* ---- Polarity ------------------------------------------------------------ */
#define BUS_OUT_ASSERT          GPIO_PIN_RESET  /* MCU low -> LS07 pulls the bus low */
#define BUS_OUT_RELEASE         GPIO_PIN_SET
#define BUS_IN_ASSERTED         GPIO_PIN_SET    /* bus low -> LVC14 output high */
#define FLUX_EDGE               TIM_INPUTCHANNELPOLARITY_RISING

/** Drive a bus output: true = assert (bus low), false = release. */
static inline void bus_out(const gpio_pin_t* p, bool assert)
{
    HAL_GPIO_WritePin(p->port, p->pin, assert ? BUS_OUT_ASSERT : BUS_OUT_RELEASE);
}

/** Read a bus input: true = bus line asserted (low on the cable). */
static inline bool bus_in(const gpio_pin_t* p)
{
    return HAL_GPIO_ReadPin(p->port, p->pin) == BUS_IN_ASSERTED;
}

/** True if the output pin currently asserts its bus line. */
static inline bool bus_out_state(const gpio_pin_t* p)
{
    return (p->port->ODR & p->pin) == 0;
}

static inline void led_set(const gpio_pin_t* p, bool on)
{
    HAL_GPIO_WritePin(p->port, p->pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/** Configure every board GPIO (clocks, modes, safe idle levels). */
void board_gpio_init(void);

#endif /* BOARD_H */
