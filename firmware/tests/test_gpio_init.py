"""board_gpio_init() of board_headless.c against a GPIO model (P06).

The pin table and the function are taken verbatim from the source (everything behind the
includes).  The model records per port: clock enabled, level written, mode, pull.

Rules checked for board revisions 10 (v1.0), 7 (v0.7) and 0 (BOARD_ID not readable):
- no port is touched before its clock is enabled (such a write is lost on the chip);
- every pin that becomes an output had its level written before, so it never drives the reset
  value of the output register;
- the level is the released one: Shugart outputs low on v1.0 (MOSFET gates) and high on v0.x
  (LS07), WGATE_n high and the firmware write lock closed on every board, IEC released, drive
  supplies off, LAN chip in reset with CS released, Apple port idle, LEDs off;
- with an unknown revision the Shugart outputs stay inputs, WGATE_n and the lock are still driven;
- no pin is configured twice, none is left out except those owned by another module.
"""
import re

from test_regressions import FW, run

# pins of the table that board_gpio_init() leaves to the module that uses them
ELSEWHERE = {
}


def main():
    source = (FW / 'src/board_headless.c').read_text(encoding='utf-8')
    body = source[source.index('/* FDD outputs'):]
    names = re.findall(r'^const gpio_pin_t (PIN_\w+)\s*=', body, re.M)
    assert len(names) == len(set(names)) and len(names) > 60, len(names)
    code = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef struct { uint32_t odr, written, out, in, af, pullup, pulldown, twice; bool clock; } GPIO_TypeDef;
static GPIO_TypeDef ports[7];
#define GPIOA (&ports[0])
#define GPIOB (&ports[1])
#define GPIOC (&ports[2])
#define GPIOD (&ports[3])
#define GPIOE (&ports[4])
#define GPIOF (&ports[5])
#define GPIOG (&ports[6])
#define __HAL_RCC_GPIOA_CLK_ENABLE() (ports[0].clock = true)
#define __HAL_RCC_GPIOB_CLK_ENABLE() (ports[1].clock = true)
#define __HAL_RCC_GPIOC_CLK_ENABLE() (ports[2].clock = true)
#define __HAL_RCC_GPIOD_CLK_ENABLE() (ports[3].clock = true)
#define __HAL_RCC_GPIOE_CLK_ENABLE() (ports[4].clock = true)
#define __HAL_RCC_GPIOF_CLK_ENABLE() (ports[5].clock = true)
#define __HAL_RCC_GPIOG_CLK_ENABLE() (ports[6].clock = true)
#define GPIO_PIN_0 0x0001u
#define GPIO_PIN_1 0x0002u
#define GPIO_PIN_2 0x0004u
#define GPIO_PIN_3 0x0008u
#define GPIO_PIN_4 0x0010u
#define GPIO_PIN_5 0x0020u
#define GPIO_PIN_6 0x0040u
#define GPIO_PIN_7 0x0080u
#define GPIO_PIN_8 0x0100u
#define GPIO_PIN_9 0x0200u
#define GPIO_PIN_10 0x0400u
#define GPIO_PIN_11 0x0800u
#define GPIO_PIN_12 0x1000u
#define GPIO_PIN_13 0x2000u
#define GPIO_PIN_14 0x4000u
#define GPIO_PIN_15 0x8000u
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { GPIO_TypeDef* port; uint16_t pin; } gpio_pin_t;
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
enum { GPIO_MODE_INPUT = 0, GPIO_MODE_OUTPUT_PP = 1, GPIO_MODE_AF_PP = 2 };
enum { GPIO_NOPULL = 0, GPIO_PULLUP = 1, GPIO_PULLDOWN = 2 };
enum { GPIO_SPEED_FREQ_LOW = 0, GPIO_SPEED_FREQ_MEDIUM, GPIO_SPEED_FREQ_HIGH, GPIO_SPEED_FREQ_VERY_HIGH };
enum { GPIO_AF1_TIM2 = 1 };
#define FDD_RDATA_AF GPIO_AF1_TIM2
#define FDD_INDEX_AF GPIO_AF1_TIM2
#define APL_RDDATA_AF GPIO_AF1_TIM2
#define BUS_OUT_RELEASE GPIO_PIN_SET

static uint8_t rev;
static uint8_t ufi_board_rev(void) { return rev; }
static unsigned unlatched, unclocked;       /* rule violations, counted so that one run lists them all */
static void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint32_t pins, GPIO_PinState state)
{
    if (!port->clock) { unclocked++; printf("write to port %c before its clock\n", (char)('A' + (port - ports))); }
    if (state == GPIO_PIN_SET) port->odr |= pins; else port->odr &= ~pins;
    port->written |= pins;
}
static void HAL_GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* g)
{
    const char name = (char)('A' + (port - ports));
    if (!port->clock) { unclocked++; printf("init of port %c before its clock\n", name); }
    assert(g->Pin && g->Pin <= 0xFFFFu && g->Mode <= GPIO_MODE_AF_PP && g->Pull <= GPIO_PULLDOWN);
    port->twice |= g->Pin & (port->out | port->in | port->af);
    if (g->Mode == GPIO_MODE_OUTPUT_PP) {
        if (g->Pin & ~port->written) { unlatched++; printf("P%c mask %04x becomes an output without a level\n", name, (unsigned)(g->Pin & ~port->written)); }
        assert(g->Pull == GPIO_NOPULL);
        port->out |= g->Pin;
    } else if (g->Mode == GPIO_MODE_AF_PP) {
        assert(g->Alternate == GPIO_AF1_TIM2);
        port->af |= g->Pin;
    } else {
        port->in |= g->Pin;
    }
    if (g->Pull == GPIO_PULLUP) port->pullup |= g->Pin;
    if (g->Pull == GPIO_PULLDOWN) port->pulldown |= g->Pin;
}
''' + body + r'''
typedef struct { const char* name; const gpio_pin_t* pin; } named_t;
static const named_t all[] = {
''' + ''.join('    {"%s", &%s},\n' % (n, n) for n in names) + r'''};
static const char* const elsewhere[] = {''' + ''.join('"%s", ' % n for n in sorted(ELSEWHERE)) + r'''NULL};

static void boot(uint8_t r) { memset(ports, 0, sizeof ports); rev = r; unlatched = unclocked = 0; board_gpio_init(); }
static bool out_high(const gpio_pin_t* p) { return (p->port->out & p->pin) && (p->port->odr & p->pin); }
static bool out_low(const gpio_pin_t* p) { return (p->port->out & p->pin) && !(p->port->odr & p->pin); }
static bool input(const gpio_pin_t* p) { return (p->port->in & p->pin) && !(p->port->out & p->pin) && !(p->port->af & p->pin); }
static bool pulled_up(const gpio_pin_t* p) { return input(p) && (p->port->pullup & p->pin) && !(p->port->pulldown & p->pin); }
static bool pulled_down(const gpio_pin_t* p) { return input(p) && (p->port->pulldown & p->pin) && !(p->port->pullup & p->pin); }
static bool floating(const gpio_pin_t* p) { return input(p) && !((p->port->pullup | p->port->pulldown) & p->pin); }
static bool capture(const gpio_pin_t* p) { return (p->port->af & p->pin) && !(p->port->out & p->pin); }

int main(void)
{
    static const uint8_t revs[] = {10, 7, 0};
    for (unsigned k = 0; k < 3; k++) {
        const uint8_t r = revs[k];
        boot(r);
        assert(!unlatched && !unclocked);
        assert(board_fdd_mosfet == (r >= 10u));
        for (unsigned p = 0; p < 7; p++) {
            if (ports[p].twice) printf("rev %u: P%c mask %04x configured twice\n", r, 'A' + p, (unsigned)ports[p].twice);
            assert(!ports[p].twice && !(ports[p].out & ports[p].in) && !(ports[p].out & ports[p].af) && !(ports[p].in & ports[p].af));
        }

        /* Shugart outputs: released, by revision; not driven at all when the revision is unknown */
        const gpio_pin_t* const shugart[] = {&PIN_FDD_MOTOR_A, &PIN_FDD_MOTOR_B, &PIN_FDD_DRV_SEL_A, &PIN_FDD_DRV_SEL_B, &PIN_FDD_STEP,
            &PIN_FDD_DIR, &PIN_FDD_SIDE_SEL, &PIN_FDD_DENSITY, &PIN_FDD_DRATE, &PIN_FDD_WDATA, &PIN_INDEX_SIM};
        for (unsigned i = 0; i < sizeof shugart / sizeof shugart[0]; i++) {
            const bool ok = r >= 10u ? out_low(shugart[i]) : r ? out_high(shugart[i]) : floating(shugart[i]);
            if (!ok) printf("rev %u: Shugart output %u not released\n", r, i);
            assert(ok);
        }
        /* the write gate and the firmware lock do not depend on the revision reading */
        assert(out_high(&PIN_FDD_WGATE) && out_high(&PIN_WLOCK));
        /* IEC released, LEDs off, drive supplies off */
        assert((GPIOD->out & 0x1Fu) == 0x1Fu && (GPIOD->odr & 0x1Fu) == 0x1Fu);
        assert((GPIOG->out & 0x0Fu) == 0x0Fu && !(GPIOG->odr & 0x0Fu));
        assert(out_low(&PIN_FDD5_EN) && out_low(&PIN_FDD12_EN));
        /* v1.0 extras idle whatever the board is: pin 4 / pin 33 drivers, Mac lines, loopback, guard */
        assert(out_low(&PIN_FDD_PIN4) && out_low(&PIN_FDD_PIN33) && out_low(&PIN_MAC_SEL) && out_low(&PIN_MAC_PWM));
        assert(out_low(&PIN_LOOP_EN) && out_low(&PIN_GUARD_OFF));
        /* LAN chip held in reset, chip select released, SPI pins not driven */
        assert(out_low(&PIN_LAN_RST) && out_high(&PIN_LAN_CS));
        assert(floating(&PIN_LAN_SCK) && floating(&PIN_LAN_MISO) && floating(&PIN_LAN_MOSI) && floating(&PIN_LAN_INT));
        /* Apple port: phases off, enables and write request released, write data low */
        assert(out_low(&PIN_APL_PH0) && out_low(&PIN_APL_PH1) && out_low(&PIN_APL_PH2) && out_low(&PIN_APL_PH3));
        assert(out_high(&PIN_APL_EN1) && out_high(&PIN_APL_EN2) && out_high(&PIN_APL_WRREQ) && out_low(&PIN_APL_WRDATA));
        assert(floating(&PIN_APL_WRPROT) && capture(&PIN_APL_RDDATA));
        /* inputs: buttons pulled up, mode switch pulled down, eFuse faults pulled up, the rest driven by the board */
        assert(pulled_up(&PIN_BTN_A) && pulled_up(&PIN_BTN_B) && pulled_down(&PIN_EXP_IO1) && pulled_down(&PIN_EXP_IO2));
        assert(pulled_up(&PIN_FLT5) && pulled_up(&PIN_FLT12) && floating(&PIN_WREN_SENSE) && floating(&PIN_WGATE_SENSE));
        assert(floating(&PIN_FDD_TRACK0) && floating(&PIN_FDD_WPROT) && floating(&PIN_FDD_DKCHG) && floating(&PIN_FDD_READY));
        assert(capture(&PIN_FDD_RDATA) && capture(&PIN_FDD_INDEX));
        /* v1.0 moved motor B and the drive LED to port C: released / off there, untouched on older boards */
        assert((GPIOC->out & (GPIO_PIN_13 | GPIO_PIN_7)) == (r >= 10u ? (GPIO_PIN_13 | GPIO_PIN_7) : 0u) && !(GPIOC->odr & (GPIO_PIN_13 | GPIO_PIN_7)));

        /* every pin of the table is set up here or named as another module's */
        for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++) {
            const gpio_pin_t* p = all[i].pin;
            const bool done = ((p->port->out | p->port->in | p->port->af) & p->pin) != 0;
            bool listed = false;
            for (unsigned e = 0; elsewhere[e]; e++) listed |= !strcmp(elsewhere[e], all[i].name);
            if (r == 10u && done == listed) printf("%s: %s\n", all[i].name, done ? "set up here but listed as another module's" : "not set up");
            if (r == 10u) assert(done != listed || (r < 10u));
        }
    }
    puts("gpio init: clocks first, levels before outputs, released levels for v1.0 / v0.x / unknown revision");
    return 0;
}
'''
    run('regression_gpio_init', code)


if __name__ == '__main__':
    main()
