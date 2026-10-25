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
#define BOARD_HAS_APPLE         1       /* Apple Disk II port J14 from v0.7 (checked at run time) */

typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
} gpio_pin_t;

/* ---- FDD outputs (34-pin J6 + Amiga J7), GPIOE ------------------------- */
extern const gpio_pin_t PIN_FDD_MOTOR_A;    /* PE7  */
extern const gpio_pin_t PIN_FDD_MOTOR_B;    /* PE8  (Amiga MTRXD) on v0.x; v1.0: PC13 (B3a), see board_pin() */
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

/* ---- Apple Disk II port J14 (board v0.7) ----------------------------------
 * Push-pull GPIOs into an SN74AHCT244 (non-inverting, 5 V TTL): PHx high = phase on,
 * EN1/EN2/WRREQ low = asserted.  Inputs through a 74LVC2G17 (non-inverting): WRPROT
 * high = protected.  RDDATA on TIM2_CH3 (same counter as RDATA), WRDATA = TIM3_CH2 in
 * toggle mode (Disk II writes a level change per flux transition). */
extern const gpio_pin_t PIN_APL_PH0;        /* PF12 */
extern const gpio_pin_t PIN_APL_PH1;        /* PF13 */
extern const gpio_pin_t PIN_APL_PH2;        /* PG6 */
extern const gpio_pin_t PIN_APL_PH3;        /* PF15 */
extern const gpio_pin_t PIN_APL_EN1;        /* PG5, /ENABLE drive 1 (J14 pin 14) */
extern const gpio_pin_t PIN_APL_EN2;        /* PB12, /ENABLE drive 2 (J15) */
extern const gpio_pin_t PIN_APL_WRREQ;      /* PB14 */
extern const gpio_pin_t PIN_APL_WRPROT;     /* PB15 */
extern const gpio_pin_t PIN_APL_RDDATA;     /* PA2, TIM2_CH3 (AF1) */
extern const gpio_pin_t PIN_APL_WRDATA;     /* PA7, TIM3_CH2 (AF2) */
#define APL_RDDATA_AF           GPIO_AF1_TIM2
#define APL_WRDATA_AF           GPIO_AF2_TIM3

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
extern const gpio_pin_t PIN_LED_FDD;        /* PG1 on v0.x; v1.0: PC7 (B3a), see board_pin() */
extern const gpio_pin_t PIN_LED_USB;        /* PG2 */
extern const gpio_pin_t PIN_LED_ERR;        /* PG3 */
extern const gpio_pin_t PIN_PWR_SRC;        /* PG4, TPS2116 status input */
extern const gpio_pin_t PIN_VBUS_SENSE;     /* PA9, 22k/33k divider */

/* v0.5: drive supply switches (high = on, 100k pull-downs keep them off in reset),
 * write lock jumper, buttons, expansion GPIO (v0.6: SD NAND on SDMMC2, no card detect) */
extern const gpio_pin_t PIN_FDD5_EN;        /* PE2; v1.0 (A11c): ENUV of the TPS25921A eFuse U28 through R135 10k (R28 100k
                                               pull-down keeps the rail off in reset), v0.x: gate of Q2 */
extern const gpio_pin_t PIN_FDD12_EN;       /* PE3; v1.0: ENUV of U29 through R141 10k, v0.x: gate of Q4 */
extern const gpio_pin_t PIN_FLT5;           /* PF9, v1.0 (A11c): U28 FLT, open drain with R137 10k to +3V3; low = undervoltage,
                                               overvoltage or thermal shutdown - also low while the rail is disabled (ENUV low
                                               counts as undervoltage), so only read while the rail is on. v0.x: unconnected */
extern const gpio_pin_t PIN_FLT12;          /* PF10, v1.0: U29 FLT (R143) */

/* v1.0 stage A11d (docs/Timer_Zuteilung.md): sync sensor, J6 pin 2/4/33, rail voltages, Mac port.
 * All of them are inputs or released outputs until a driver uses them (stage A10). */
extern const gpio_pin_t PIN_SYNC;           /* PC6, TIM8_CH1 (AF3): J19 pin 3 through U26 (74LVC14), high = sensor pulse;
                                               own line since A11d (was wired onto the INDEX bus) */
extern const gpio_pin_t PIN_FDD_PIN2_IN;    /* PD14: J6 pin 2 read back through U26 (high = line low) */
extern const gpio_pin_t PIN_FDD_PIN4_IN;    /* PD15: J6 pin 4 read back through U26 */
extern const gpio_pin_t PIN_FDD_PIN4;       /* PF14: J6 pin 4 output, MOSFET Q12 gate (bus_out() semantics, 100k R158) */
extern const gpio_pin_t PIN_FDD_PIN33;      /* PB5: J6 pin 33 flippy signal through R156 2.2k (R157 2.2k to GND), only
                                               reaches the connector when the solder jumper JP7 is set to FLIPPY */
extern const gpio_pin_t PIN_MAC_SEL;        /* PB0: Mac drive SEL (head select) through U27 (AHCT244) to J23 pin 12 */
extern const gpio_pin_t PIN_MAC_PWM;        /* PA8, TIM1_CH1 (AF1): 400K drive speed PWM through U27 and the solder
                                               jumper JP5 (open by default) to J23 pin 20 */
extern const gpio_pin_t PIN_MAC_CSTOUT;     /* PB1: J23 pin 20 read back through U26 (800K/FDHD !CSTOUT, high = asserted) */
#define ADC_CH_V_FDD5           ADC_CHANNEL_4   /* PC4: FDD_5V through R160/R161 10k/10k (x2) */
#define ADC_CH_V_FDD12          ADC_CHANNEL_8   /* PC5: FDD_12V through R162/R163 33k/10k (x4.3) */
extern const gpio_pin_t PIN_WLOCK;          /* v0.5-v0.7: PE4 input, high = WRITE LOCK jumper set (J11/JP4).  v1.0 (A9b): the
                                               same pin PE4 is the OUTPUT WLOCK_FW (high = firmware lock, 100k pull-up R131
                                               keeps it high through reset) into the 3-input OR U14; the hardware lock state
                                               comes back on PIN_WREN_SENSE.  ufi_board_jumper_locked() reads the right pin. */
extern const gpio_pin_t PIN_WREN_SENSE;     /* PE6 input (v1.0): WLOCK_HW through R130 470 R; high = front switch open or
                                               cable off = write locked (R33 10k pull-up), low = switch SCHREIBEN closed */
extern const gpio_pin_t PIN_FDD_INDEX2;     /* PB11, TIM2_CH4 (AF1), v1.0: INDEX also on channel 4 so that CH2 can measure the
                                               RDATA pulse width on the same time base (capture code still uses CH2) */
/* ufi_board_write_enable(bool) - v1.0: WLOCK_FW low only while a write runs - is declared in ufi_firmware.h */
extern const gpio_pin_t PIN_BTN_A;          /* PB8, low = pressed (v1.0: front header J12 pin 13) */
extern const gpio_pin_t PIN_BTN_B;          /* PB9, low = pressed (v1.0: front header J12 pin 14) */
extern const gpio_pin_t PIN_EXP_IO1;        /* PE0, mode switch "USB floppy" (v0.7 J13 pin 1 / J9 pin 5, v1.0 J12 pin 15) */
extern const gpio_pin_t PIN_EXP_IO2;        /* PE1, mode switch "SD drive" (v0.7 J13 pin 3 / J9 pin 6, v1.0 J12 pin 16) */

/* v1.0 board (closed enclosure, nothing pluggable after assembly): the index simulation has
 * its own timer output and the self-test loopback is switched electronically.  On v0.7
 * boards these pins are unconnected: the outputs drive nothing, the inputs float. */
extern const gpio_pin_t PIN_INDEX_SIM;      /* PE5, TIM15_CH1 (AF4): low pulse -> LS07 U7F pulls the INDEX bus low */
extern const gpio_pin_t PIN_LOOP_EN;        /* PG14, high = TS5A3166 closes WDATA->RDATA, 2N7002 pulls TRK0 low */
extern const gpio_pin_t PIN_WGATE_SENSE;    /* PG15, WGATE bus read back through HC14 U10F (high = WGATE asserted) */
extern const gpio_pin_t PIN_GUARD_OFF;      /* PG7, J20 pin 2: C64 PSU Guard OFF (high = both drive rails off) */
extern const gpio_pin_t PIN_GUARD_ERR5;     /* PG8, J20 pin 3: Guard 5 V channel tripped (open drain, 10k pull-up R107) */
extern const gpio_pin_t PIN_GUARD_ERR12;    /* PC9 (B3a, was PG13), J20 pin 4: Guard 12 V channel tripped (10k pull-up R108) */
#define INDEX_SIM_AF            GPIO_AF4_TIM15  /* TIM15_CH1 on PE5 */

/* v1.0 LAN: WIZnet W5500 on SPI3 (schematic sheet LAN, stage A6).  No driver yet: the
 * board init only parks the chip (RST low, CS high, SPI pins as inputs).  On v0.7 boards
 * the pins are unconnected. */
extern const gpio_pin_t PIN_LAN_SCK;        /* PC10, SPI3_SCK (AF6) */
extern const gpio_pin_t PIN_LAN_MISO;       /* PC11, SPI3_MISO (AF6) */
extern const gpio_pin_t PIN_LAN_MOSI;       /* PC12, SPI3_MOSI (AF6) */
extern const gpio_pin_t PIN_LAN_CS;         /* PA15, W5500 SCSn, low = selected (10k pull-up R119) */
extern const gpio_pin_t PIN_LAN_INT;        /* PD5, W5500 INTn, low = interrupt (10k pull-up R120) */
extern const gpio_pin_t PIN_LAN_RST;        /* PD10, W5500 RSTn, low >= 500 us = reset (10k pull-up R121) */
#define LAN_SPI_AF              GPIO_AF6_SPI3
#define ADC_CH_I_FDD5           ADC_CHANNEL_10  /* PC0, INA180A1: 2 V/A (0.1R, v0.x) / 1 V/A (50 mR, v1.0) */
#define ADC_CH_I_FDD12          ADC_CHANNEL_11  /* PC1 */
#define ADC_CH_BOARD_ID         ADC_CHANNEL_18  /* PA4: board revision divider */
#define BOARD_ID_V05_MV         1650u
#define BOARD_ID_V06_MV         1055u   /* 10k/4.7k, SD NAND instead of the microSD slot */
#define BOARD_ID_V07_MV         595u    /* 10k/2.2k, Apple Disk II port */
#define BOARD_ID_V10_MV         2269u   /* 10k/22k, v1.0: front header, electronic loopback, LAN (rev 10) */

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
#define BUS_OUT_ASSERT          GPIO_PIN_RESET  /* MCU low -> LS07 pulls the bus low (IEC on every board, Shugart on v0.x) */
#define BUS_OUT_RELEASE         GPIO_PIN_SET
#define BUS_IN_ASSERTED         GPIO_PIN_SET    /* bus low -> LVC14 output high */
#define FLUX_EDGE               TIM_INPUTCHANNELPOLARITY_RISING

/* v1.0 (A11b): the twelve Shugart/Amiga outputs (PE7-PE15, PF11 DRATE, PE5 INDEX_SIM and the TIM3
 * output PA6 WDATA) drive the gates of the dual N-MOSFETs Q6-Q11 (BSS138DW, 100k gate pull-downs):
 * MCU high = bus low, reset = released.  The IEC outputs (PD0-PD4) keep the LS07 U9 and its polarity.
 * board_gpio_init() sets the flag from the board revision before it touches any bus output. */
extern bool board_fdd_mosfet;

/* Stage B3a (pin swap for the v1.0 routing): on v1.0 FDD_MOTOR_B moved from PE8 to PC13 and LED_FDD
 * from PG1 to PC7.  PIN_FDD_MOTOR_B / PIN_LED_FDD keep the v0.x pins so the bench boards keep working;
 * bus_out(), bus_out_state() and led_set() translate them here (board_fdd_mosfet = board v1.0).
 * PC13 is a backup-domain pin (low speed, 3 mA): enough for the static Q6 gate (100k pull-down). */
static inline const gpio_pin_t* board_pin(const gpio_pin_t* p, gpio_pin_t* v10)
{
    if (board_fdd_mosfet) {
        if (p->port == GPIOE && p->pin == GPIO_PIN_8) {
            v10->port = GPIOC; v10->pin = GPIO_PIN_13; return v10;
        }
        if (p->port == GPIOG && p->pin == GPIO_PIN_1) {
            v10->port = GPIOC; v10->pin = GPIO_PIN_7; return v10;
        }
    }
    return p;
}

static inline bool bus_out_inverted(const gpio_pin_t* p)
{
    /* A11f: WGATE keeps the active-low convention on v1.0 as well - PE14 is WGATE_n into the NOR
     * gate U31 (Q9 gate = NOR(WGATE_n, LOCK), R167 10k pull-up), so the lock can never assert it */
    return board_fdd_mosfet && p->port != GPIOD && !(p->port == GPIOE && p->pin == GPIO_PIN_14);
}

/** Drive a bus output: true = assert (bus low), false = release. */
static inline void bus_out(const gpio_pin_t* p, bool assert)
{
    gpio_pin_t v10;
    p = board_pin(p, &v10);
    HAL_GPIO_WritePin(p->port, p->pin, (assert != bus_out_inverted(p)) ? BUS_OUT_ASSERT : BUS_OUT_RELEASE);
}

/** Read a bus input: true = bus line asserted (low on the cable). */
static inline bool bus_in(const gpio_pin_t* p)
{
    return HAL_GPIO_ReadPin(p->port, p->pin) == BUS_IN_ASSERTED;
}

/** True if the output pin currently asserts its bus line. */
static inline bool bus_out_state(const gpio_pin_t* p)
{
    gpio_pin_t v10;
    p = board_pin(p, &v10);
    return ((p->port->ODR & p->pin) == 0) != bus_out_inverted(p);
}

static inline void led_set(const gpio_pin_t* p, bool on)
{
    gpio_pin_t v10;
    p = board_pin(p, &v10);
    HAL_GPIO_WritePin(p->port, p->pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/** Configure every board GPIO (clocks, modes, safe idle levels). */
void board_gpio_init(void);

#endif /* BOARD_H */
