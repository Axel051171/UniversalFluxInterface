/**
 * UFI Flux Engine - board v1.0 sensing (stage A10): calendar access for the host, SHT40
 * temperature/humidity sensor, drive supply voltages, J6 pin read-backs, recording metadata.
 *
 * SHT40 (U25, A9c): I2C1 on PB6 (SCL) / PB7 (SDA), AF4, open drain, 2.2k pull-ups R19/R20 on
 * the board, address 0x44.  Kernel clock HSI 64 MHz (always on: the ADC runs from it too),
 * TIMINGR for 100 kHz from the reference manual table (PRESC 15 -> 4 MHz, SCLL 0x13,
 * SCLH 0x0F, SDADEL 2, SCLDEL 4).  Command 0xFD = measure with high repeatability (8.2 ms
 * max), six result bytes T_hi T_lo CRC RH_hi RH_lo CRC, CRC-8 polynomial 0x31 (datasheet
 * 4.4 / 4.5).  Every transfer has a 10 ms timeout, so a missing sensor (v0.x boards, where
 * PB6/PB7 only reach J9) ends in SHT40_NO_SENSOR after about 20 ms, never in a hang.
 *
 * Rail voltages (A11d S4): PC4 = FDD_5V through R160/R161 10k/10k, PC5 = FDD_12V through
 * R162/R163 33k/10k; source impedance 5 / 7.7 kOhm, ufi_adc_mv() samples 64.5 cycles.
 * Limits 4.5 V / 10.8 V (RAIL_FLAG_*_LOW), reported by SELFTEST, op 0x45 and the metadata.
 *
 * State lives in DTCM (.dtcm is NOLOAD, so ufi_sense_init() clears it): the .bss/.data
 * budget in AXI SRAM is frozen at the gate value.
 */

#include <stdio.h>
#include <string.h>

#include "ufi_firmware.h"

#ifndef UFI_FW_VERSION
#define UFI_FW_VERSION "0.0"
#endif

#define SHT40_ADDR          (0x44u << 1)
#define SHT40_CMD_MEASURE   0xFDu       /* high repeatability */
#define SHT40_MEASURE_MS    10u         /* datasheet: 8.2 ms max */
#define I2C_XFER_TIMEOUT_MS 10u
#define I2C1_TIMING_100K    0xF0420F13u /* HSI 64 MHz: PRESC 15, SCLDEL 4, SDADEL 2, SCLH 0x0F, SCLL 0x13 */

typedef struct {
    I2C_HandleTypeDef i2c;
    bool i2c_ok;                /* HAL_I2C_Init succeeded */
    bool sht_present;           /* probe at start-up answered with a valid CRC */
    bool v10;                   /* board_rev >= 10: dividers, read-backs, outputs fitted */
    uint8_t pins_out;           /* bit0 J6 pin 4 asserted, bit1 pin 33 high */
} sense_state_t;

__attribute__((section(".dtcm"), aligned(4))) static sense_state_t st;

/* ============================================================================
 * CALENDAR
 * ============================================================================ */

bool ufi_sense_rtc_get(ufi_rtc_time_t* t)
{
    memset(t, 0, sizeof(*t));
    t->ready = ufi_clock_rtc_get(&t->year, &t->mon, &t->day, &t->hour, &t->min, &t->sec);
    if (!t->ready) {
        t->year = 0;
        t->mon = t->day = t->hour = t->min = t->sec = 0;
    }
    return t->ready;
}

int ufi_sense_rtc_set(const ufi_rtc_time_t* t)
{
    if (!ufi_clock_rtc_ready()) {
        return UFI_ERR_NOT_IMPL;
    }
    if (!ufi_calendar_valid(t->year, t->mon, t->day, t->hour, t->min, t->sec)) {
        return UFI_ERR_BAD_ARGS;
    }
    return ufi_clock_rtc_set(t->year, t->mon, t->day, t->hour, t->min, t->sec) ? UFI_OK : UFI_ERR_TIMEOUT;
}

/* ============================================================================
 * SHT40
 * ============================================================================ */

static void i2c_init(void)
{
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
    pclk.I2c123ClockSelection = RCC_I2C123CLKSOURCE_HSI;
    (void)HAL_RCCEx_PeriphCLKConfig(&pclk);
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    g.Mode = GPIO_MODE_AF_OD;
    g.Pull = GPIO_NOPULL;                   /* R19/R20 2.2k on the board */
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &g);

    st.i2c.Instance = I2C1;
    st.i2c.Init.Timing = I2C1_TIMING_100K;
    st.i2c.Init.OwnAddress1 = 0;
    st.i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    st.i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    st.i2c.Init.OwnAddress2 = 0;
    st.i2c.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    st.i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    st.i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    st.i2c_ok = HAL_I2C_Init(&st.i2c) == HAL_OK &&
                HAL_I2CEx_ConfigAnalogFilter(&st.i2c, I2C_ANALOGFILTER_ENABLE) == HAL_OK;
}

int ufi_sense_sht40(ufi_sht40_t* r)
{
    memset(r, 0, sizeof(*r));
    if (!st.i2c_ok) {
        r->status = SHT40_NO_BUS;
        return UFI_OK;
    }
    uint8_t cmd = SHT40_CMD_MEASURE, raw[6];
    if (HAL_I2C_Master_Transmit(&st.i2c, SHT40_ADDR, &cmd, 1, I2C_XFER_TIMEOUT_MS) != HAL_OK) {
        r->status = SHT40_NO_SENSOR;        /* NACK on the address or a stuck bus */
        return UFI_OK;
    }
    HAL_Delay(SHT40_MEASURE_MS);
    if (HAL_I2C_Master_Receive(&st.i2c, SHT40_ADDR, raw, sizeof(raw), I2C_XFER_TIMEOUT_MS) != HAL_OK) {
        r->status = SHT40_NO_SENSOR;        /* still measuring = NACK (not expected after 10 ms) */
        return UFI_OK;
    }
    r->status = ufi_sht40_convert(raw, &r->temp_x100, &r->rh_x100) ? SHT40_OK : SHT40_CRC;
    return UFI_OK;
}

bool ufi_sense_sht40_present(void)
{
    return st.sht_present;
}

/* ============================================================================
 * RAIL VOLTAGES
 * ============================================================================ */

void ufi_sense_rails(ufi_rails_t* r)
{
    memset(r, 0, sizeof(*r));
    const uint8_t on = (uint8_t)(ufi_board_status().power & 0x03u);
    r->flags = (uint8_t)((on & 1u ? RAIL_FLAG_5V_ON : 0u) | (on & 2u ? RAIL_FLAG_12V_ON : 0u));
    if (!st.v10) {
        return;                             /* no dividers: PC4/PC5 float on v0.x */
    }
    r->flags |= RAIL_FLAG_FITTED;
    r->v5_mv = ufi_rail_mv(ufi_adc_mv(ADC_CH_V_FDD5), 0);
    r->v12_mv = ufi_rail_mv(ufi_adc_mv(ADC_CH_V_FDD12), 1);
    if ((on & 1u) && r->v5_mv < RAIL_5V_MIN_MV) {
        r->flags |= RAIL_FLAG_5V_LOW;
    }
    if ((on & 2u) && r->v12_mv < RAIL_12V_MIN_MV) {
        r->flags |= RAIL_FLAG_12V_LOW;
    }
}

/* ============================================================================
 * METADATA
 * ============================================================================ */

void ufi_sense_meta(ufi_sense_meta_t* m)
{
    memset(m, 0, sizeof(*m));
    ufi_rtc_time_t t;
    m->rtc = ufi_sense_rtc_get(&t);
    ufi_calendar_text(m->time, &t);
    if (st.v10) {
        ufi_rails_t r;
        ufi_sense_rails(&r);
        m->rails = true;
        m->v5_mv = r.v5_mv;
        m->v12_mv = r.v12_mv;
    }
    if (st.sht_present) {
        ufi_sht40_t s;
        (void)ufi_sense_sht40(&s);
        if (s.status == SHT40_OK) {
            m->sensor = true;
            ufi_sht40_text(m->temp_c, m->rh, s.temp_x100, s.rh_x100);
        }
    }
}

uint16_t ufi_sense_log_header(char* buf, uint16_t n)
{
    ufi_sense_meta_t m;
    ufi_sense_meta(&m);
    int len = snprintf(buf, n, "UFI dump board %u fw " UFI_FW_VERSION " %s", (unsigned)ufi_board_rev(),
                       m.rtc ? m.time : "no-rtc");
    if (len > 0 && m.rails && len < (int)n) {
        len += snprintf(buf + len, n - (unsigned)len, " 5V %u mV 12V %u mV", m.v5_mv, m.v12_mv);
    }
    if (len > 0 && m.sensor && len < (int)n) {
        len += snprintf(buf + len, n - (unsigned)len, " %s C %s %%RH", m.temp_c, m.rh);
    }
    if (len < 0) {
        len = 0;
    }
    if (len > (int)n - 3) {
        len = (int)n - 3;
    }
    buf[len++] = '\r';
    buf[len++] = '\n';
    buf[len] = 0;
    return (uint16_t)len;
}

/* ============================================================================
 * J6 PIN READ-BACKS AND OUTPUTS (A11d)
 * ============================================================================ */

uint8_t ufi_sense_fdd_pins(void)
{
    if (!st.v10) {
        return 0;
    }
    uint8_t m = 0;
    if (bus_in(&PIN_FDD_PIN2_IN)) m |= FDD_PIN_PIN2_IN;
    if (bus_in(&PIN_FDD_PIN4_IN)) m |= FDD_PIN_PIN4_IN;
    if (bus_in(&PIN_MAC_CSTOUT)) m |= FDD_PIN_CSTOUT;
    if (bus_in(&PIN_WGATE_SENSE)) m |= FDD_PIN_WGATE_SENSE;
    if (HAL_GPIO_ReadPin(PIN_WREN_SENSE.port, PIN_WREN_SENSE.pin) == GPIO_PIN_SET) m |= FDD_PIN_WREN_SENSE;
    if (HAL_GPIO_ReadPin(PIN_FLT5.port, PIN_FLT5.pin) == GPIO_PIN_RESET) m |= FDD_PIN_FLT5;
    if (HAL_GPIO_ReadPin(PIN_FLT12.port, PIN_FLT12.pin) == GPIO_PIN_RESET) m |= FDD_PIN_FLT12;
    if (HAL_GPIO_ReadPin(PIN_SYNC.port, PIN_SYNC.pin) == GPIO_PIN_SET) m |= FDD_PIN_SYNC;
    return m;
}

int ufi_sense_fdd_pins_set(uint8_t pin4, uint8_t pin33)
{
    if (!st.v10) {
        return UFI_ERR_NOT_IMPL;
    }
    if ((pin4 > 1u && pin4 != 0xFFu) || (pin33 > 1u && pin33 != 0xFFu)) {
        return UFI_ERR_BAD_ARGS;
    }
    if (pin4 != 0xFFu) {
        bus_out(&PIN_FDD_PIN4, pin4 != 0u);     /* Q12 gate: bus_out() knows the MOSFET polarity */
        st.pins_out = (uint8_t)((st.pins_out & ~0x01u) | (pin4 ? 0x01u : 0u));
    }
    if (pin33 != 0xFFu) {
        HAL_GPIO_WritePin(PIN_FDD_PIN33.port, PIN_FDD_PIN33.pin, pin33 ? GPIO_PIN_SET : GPIO_PIN_RESET);
        st.pins_out = (uint8_t)((st.pins_out & ~0x02u) | (pin33 ? 0x02u : 0u));
    }
    return UFI_OK;
}

uint8_t ufi_sense_fdd_pins_out(void)
{
    return st.pins_out;
}

/* ============================================================================
 * INIT
 * ============================================================================ */

void ufi_sense_init(void)
{
    memset(&st, 0, sizeof(st));
    st.v10 = ufi_board_rev() >= 10u;

    /* PC4/PC5 analog on every board (unconnected before v1.0) */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &g);

    i2c_init();
    ufi_sht40_t s;
    (void)ufi_sense_sht40(&s);                  /* about 20 ms; FEATURES id 12 and the metadata rely on it */
    st.sht_present = (s.status == SHT40_OK);
}
