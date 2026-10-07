/**
 * UFI Flux Engine - Board GPIO: UFI Headless
 *
 * Pin table and GPIO setup; see board.h for polarity rules.
 */

#include "board.h"

/* FDD outputs (LS07 open-collector drivers) */
const gpio_pin_t PIN_FDD_MOTOR_A     = {GPIOE, GPIO_PIN_7};
const gpio_pin_t PIN_FDD_MOTOR_B     = {GPIOE, GPIO_PIN_8};
const gpio_pin_t PIN_FDD_DRV_SEL_A   = {GPIOE, GPIO_PIN_9};
const gpio_pin_t PIN_FDD_DRV_SEL_B   = {GPIOE, GPIO_PIN_10};
const gpio_pin_t PIN_FDD_STEP        = {GPIOE, GPIO_PIN_11};
const gpio_pin_t PIN_FDD_DIR         = {GPIOE, GPIO_PIN_12};
const gpio_pin_t PIN_FDD_SIDE_SEL    = {GPIOE, GPIO_PIN_13};
const gpio_pin_t PIN_FDD_WGATE       = {GPIOE, GPIO_PIN_14};
const gpio_pin_t PIN_FDD_DENSITY     = {GPIOE, GPIO_PIN_15};
const gpio_pin_t PIN_FDD_DRATE       = {GPIOF, GPIO_PIN_11};
const gpio_pin_t PIN_FDD_WDATA       = {GPIOA, GPIO_PIN_6};

/* FDD inputs (LVC14 inverters) */
const gpio_pin_t PIN_FDD_RDATA       = {GPIOA, GPIO_PIN_5};
const gpio_pin_t PIN_FDD_INDEX       = {GPIOA, GPIO_PIN_1};
const gpio_pin_t PIN_FDD_TRACK0      = {GPIOF, GPIO_PIN_0};
const gpio_pin_t PIN_FDD_WPROT       = {GPIOF, GPIO_PIN_1};
const gpio_pin_t PIN_FDD_DKCHG       = {GPIOF, GPIO_PIN_2};
const gpio_pin_t PIN_FDD_READY       = {GPIOF, GPIO_PIN_3};

/* IEC bus */
const gpio_pin_t PIN_IEC_ATN_OUT     = {GPIOD, GPIO_PIN_0};
const gpio_pin_t PIN_IEC_CLK_OUT     = {GPIOD, GPIO_PIN_1};
const gpio_pin_t PIN_IEC_DATA_OUT    = {GPIOD, GPIO_PIN_2};
const gpio_pin_t PIN_IEC_SRQ_OUT     = {GPIOD, GPIO_PIN_3};
const gpio_pin_t PIN_IEC_RESET_OUT   = {GPIOD, GPIO_PIN_4};
const gpio_pin_t PIN_IEC_ATN_IN      = {GPIOF, GPIO_PIN_4};
const gpio_pin_t PIN_IEC_CLK_IN      = {GPIOF, GPIO_PIN_5};
const gpio_pin_t PIN_IEC_DATA_IN     = {GPIOF, GPIO_PIN_6};
const gpio_pin_t PIN_IEC_SRQ_IN      = {GPIOF, GPIO_PIN_7};
const gpio_pin_t PIN_IEC_RESET_IN    = {GPIOF, GPIO_PIN_8};

/* Status */
const gpio_pin_t PIN_LED_ACT         = {GPIOG, GPIO_PIN_0};
const gpio_pin_t PIN_LED_FDD         = {GPIOG, GPIO_PIN_1};
const gpio_pin_t PIN_LED_USB         = {GPIOG, GPIO_PIN_2};
const gpio_pin_t PIN_LED_ERR         = {GPIOG, GPIO_PIN_3};
const gpio_pin_t PIN_PWR_SRC         = {GPIOG, GPIO_PIN_4};
const gpio_pin_t PIN_VBUS_SENSE      = {GPIOA, GPIO_PIN_9};

/* v0.5 board extras */
const gpio_pin_t PIN_FDD5_EN         = {GPIOE, GPIO_PIN_2};
const gpio_pin_t PIN_FDD12_EN        = {GPIOE, GPIO_PIN_3};
const gpio_pin_t PIN_WLOCK           = {GPIOE, GPIO_PIN_4};
const gpio_pin_t PIN_BTN_A           = {GPIOB, GPIO_PIN_8};
const gpio_pin_t PIN_BTN_B           = {GPIOB, GPIO_PIN_9};
const gpio_pin_t PIN_EXP_IO1         = {GPIOE, GPIO_PIN_0};
const gpio_pin_t PIN_EXP_IO2         = {GPIOE, GPIO_PIN_1};

/* v0.7 Apple Disk II port (pins unconnected on older boards: harmless) */
const gpio_pin_t PIN_APL_PH0         = {GPIOF, GPIO_PIN_12};
const gpio_pin_t PIN_APL_PH1         = {GPIOF, GPIO_PIN_13};
const gpio_pin_t PIN_APL_PH2         = {GPIOG, GPIO_PIN_6};
const gpio_pin_t PIN_APL_PH3         = {GPIOF, GPIO_PIN_15};
const gpio_pin_t PIN_APL_EN1         = {GPIOG, GPIO_PIN_5};
const gpio_pin_t PIN_APL_EN2         = {GPIOB, GPIO_PIN_12};
const gpio_pin_t PIN_APL_WRREQ       = {GPIOB, GPIO_PIN_14};
const gpio_pin_t PIN_APL_WRPROT      = {GPIOB, GPIO_PIN_15};
const gpio_pin_t PIN_APL_RDDATA      = {GPIOA, GPIO_PIN_2};
const gpio_pin_t PIN_APL_WRDATA      = {GPIOA, GPIO_PIN_7};

static void init_pins(GPIO_TypeDef* port, uint16_t pins, uint32_t mode, uint32_t pull,
                      uint32_t speed, uint32_t alternate)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = pins;
    gpio.Mode = mode;
    gpio.Pull = pull;
    gpio.Speed = speed;
    gpio.Alternate = alternate;
    HAL_GPIO_Init(port, &gpio);
}

void board_gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* Bus outputs: latch the released level before switching to output mode,
     * so no line glitches to "asserted" during start-up. */
    const uint16_t fdd_e = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 |
                           GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    const uint16_t iec_d = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4;
    HAL_GPIO_WritePin(GPIOE, fdd_e, BUS_OUT_RELEASE);
    HAL_GPIO_WritePin(GPIOD, iec_d, BUS_OUT_RELEASE);
    HAL_GPIO_WritePin(PIN_FDD_WDATA.port, PIN_FDD_WDATA.pin, BUS_OUT_RELEASE);
    HAL_GPIO_WritePin(PIN_FDD_DRATE.port, PIN_FDD_DRATE.pin, BUS_OUT_RELEASE);
    init_pins(GPIOE, fdd_e, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_HIGH, 0);
    init_pins(PIN_FDD_DRATE.port, PIN_FDD_DRATE.pin, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,
              GPIO_SPEED_FREQ_LOW, 0);
    init_pins(GPIOD, iec_d, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_MEDIUM, 0);
    init_pins(PIN_FDD_WDATA.port, PIN_FDD_WDATA.pin, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,
              GPIO_SPEED_FREQ_VERY_HIGH, 0);

    /* Inputs are driven by the 74LVC14A: no pull resistors needed */
    init_pins(GPIOF, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                     GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8,
              GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, 0);

    /* RDATA / INDEX -> TIM2 input capture */
    init_pins(PIN_FDD_RDATA.port, PIN_FDD_RDATA.pin, GPIO_MODE_AF_PP, GPIO_NOPULL,
              GPIO_SPEED_FREQ_VERY_HIGH, FDD_RDATA_AF);
    init_pins(PIN_FDD_INDEX.port, PIN_FDD_INDEX.pin, GPIO_MODE_AF_PP, GPIO_NOPULL,
              GPIO_SPEED_FREQ_VERY_HIGH, FDD_INDEX_AF);

    /* LEDs off, status inputs */
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3, GPIO_PIN_RESET);
    init_pins(GPIOG, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3,
              GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, 0);
    init_pins(PIN_PWR_SRC.port, PIN_PWR_SRC.pin, GPIO_MODE_INPUT, GPIO_PULLUP,
              GPIO_SPEED_FREQ_LOW, 0);
    init_pins(PIN_VBUS_SENSE.port, PIN_VBUS_SENSE.pin, GPIO_MODE_INPUT, GPIO_NOPULL,
              GPIO_SPEED_FREQ_LOW, 0);

    /* v0.5: drive supplies stay off until ufi_board_init(); write lock has its own
     * pull-down, buttons use the MCU pull-ups, expansion GPIO pulled down */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2 | GPIO_PIN_3, GPIO_PIN_RESET);
    init_pins(GPIOE, GPIO_PIN_2 | GPIO_PIN_3, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, 0);
    init_pins(PIN_WLOCK.port, PIN_WLOCK.pin, GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, 0);
    init_pins(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_SPEED_FREQ_LOW, 0);
    init_pins(GPIOE, GPIO_PIN_0 | GPIO_PIN_1, GPIO_MODE_INPUT, GPIO_PULLDOWN, GPIO_SPEED_FREQ_LOW, 0);

    /* v0.7 Apple port: phases off, enables and write request released (high) before the
     * pins become outputs; RDDATA to TIM2_CH3, WRDATA left to ufi_write.c (TIM3_CH2) */
    const uint16_t apl_ph = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15;      /* PH0, PH1, PH3 */
    const uint16_t apl_ctl = GPIO_PIN_12 | GPIO_PIN_14;                   /* EN2, WRREQ */
    HAL_GPIO_WritePin(GPIOF, apl_ph, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_APL_PH2.port, PIN_APL_PH2.pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, apl_ctl, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PIN_APL_EN1.port, PIN_APL_EN1.pin, GPIO_PIN_SET);
    init_pins(GPIOF, apl_ph, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, 0);
    init_pins(GPIOB, apl_ctl, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, 0);
    init_pins(GPIOG, PIN_APL_PH2.pin | PIN_APL_EN1.pin, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,
              GPIO_SPEED_FREQ_LOW, 0);
    init_pins(PIN_APL_WRPROT.port, PIN_APL_WRPROT.pin, GPIO_MODE_INPUT, GPIO_NOPULL,
              GPIO_SPEED_FREQ_LOW, 0);
    init_pins(PIN_APL_RDDATA.port, PIN_APL_RDDATA.pin, GPIO_MODE_AF_PP, GPIO_NOPULL,
              GPIO_SPEED_FREQ_VERY_HIGH, APL_RDDATA_AF);
    HAL_GPIO_WritePin(PIN_APL_WRDATA.port, PIN_APL_WRDATA.pin, GPIO_PIN_RESET);
    init_pins(PIN_APL_WRDATA.port, PIN_APL_WRDATA.pin, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,
              GPIO_SPEED_FREQ_VERY_HIGH, 0);
}
