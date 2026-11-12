"""PSRAM start-up logic of ufi_psram.c against a simulated controller and chip.

The whole source file is compiled with a stub HAL: an APS6404L model that knows SPI and QPI
mode (READ ID only in SPI mode, the one-line reset only in SPI mode), a controller that can
fail at any chosen call, and plain host memory behind the mapped window.  Covered: the
bench faults of 2026-11-11 (chip left in QPI mode), every controller failure by phase, a
silent chip, the trial marker and the "PSRAM off" latch with each kind of reset, the safe
start, the host retest and the status block.  Not covered here: what the real OCTOSPI does
with narrow or DQS-less writes - the window is ordinary RAM in this model.
"""
from test_regressions import BASE, ERRORS, run

STUB = r'''
#define UFI_FIRMWARE_H                      /* the real header pulls the HAL; replaced below */
#include <stdlib.h>
#define BOARD_HAS_PSRAM 1
#define PSRAM_SIZE_BYTES (8u * 1024u * 1024u)

typedef struct { uint32_t pin_state; } GPIO_TypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
static GPIO_TypeDef gpiob, gpiod;
#define GPIOB (&gpiob)
#define GPIOD (&gpiod)
#define PSRAM_CLK_PORT GPIOB
#define PSRAM_CLK_PIN 0x0004u
#define PSRAM_NCS_PORT GPIOB
#define PSRAM_NCS_PIN 0x0400u
#define PSRAM_IO0_PORT GPIOD
#define PSRAM_IO0_PIN 0x0800u
#define PSRAM_IO1_PORT GPIOD
#define PSRAM_IO1_PIN 0x1000u
#define PSRAM_IO2_PORT GPIOB
#define PSRAM_IO2_PIN 0x2000u
#define PSRAM_IO3_PORT GPIOD
#define PSRAM_IO3_PIN 0x2000u
enum { GPIO_PIN_RESET, GPIO_PIN_SET };
enum { GPIO_MODE_INPUT, GPIO_MODE_OUTPUT_PP, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_PULLUP,
       GPIO_SPEED_FREQ_HIGH, GPIO_SPEED_FREQ_VERY_HIGH, GPIO_AF4_OCTOSPIM_P1, GPIO_AF9_OCTOSPIM_P1 };
static void HAL_GPIO_Init(GPIO_TypeDef* p, GPIO_InitTypeDef* g) { (void)p; (void)g; }
static void HAL_GPIO_WritePin(GPIO_TypeDef* p, uint32_t pin, int v) { (void)p; (void)pin; (void)v; }
static int HAL_GPIO_ReadPin(GPIO_TypeDef* p, uint32_t pin) { (void)p; (void)pin; return GPIO_PIN_SET; }  /* pull-up, chip silent */

static uint32_t tick;
static uint32_t HAL_GetTick(void) { return tick++; }
static void HAL_Delay(uint32_t ms) { tick += ms; }

#define HAL_OK 0
#define HAL_ERROR 1
enum { HAL_OSPI_STATE_RESET };
enum { HAL_OSPI_INSTRUCTION_1_LINE = 1, HAL_OSPI_INSTRUCTION_4_LINES = 4 };
enum { HAL_OSPI_DATA_NONE, HAL_OSPI_DATA_1_LINE, HAL_OSPI_DATA_4_LINES };
enum { HAL_OSPI_OPTYPE_COMMON_CFG, HAL_OSPI_OPTYPE_READ_CFG, HAL_OSPI_OPTYPE_WRITE_CFG };
enum { HAL_OSPI_DQS_DISABLE, HAL_OSPI_DQS_ENABLE };
enum { HAL_OSPI_ADDRESS_NONE, HAL_OSPI_ADDRESS_1_LINE, HAL_OSPI_ADDRESS_4_LINES, HAL_OSPI_ADDRESS_24_BITS,
       HAL_OSPI_ALTERNATE_BYTES_NONE, HAL_OSPI_CLOCK_MODE_0, HAL_OSPI_DELAY_BLOCK_BYPASSED, HAL_OSPI_DHQC_DISABLE,
       HAL_OSPI_DUALQUAD_DISABLE, HAL_OSPI_FLASH_ID_1, HAL_OSPI_FREERUNCLK_DISABLE, HAL_OSPI_INSTRUCTION_8_BITS,
       HAL_OSPI_INSTRUCTION_DTR_DISABLE, HAL_OSPI_IT_TO, HAL_OSPI_MEMTYPE_APMEMORY,
       HAL_OSPI_SAMPLE_SHIFTING_NONE, HAL_OSPI_SIOO_INST_EVERY_CMD, HAL_OSPI_TIMEOUT_COUNTER_ENABLE,
       HAL_OSPI_WRAP_NOT_SUPPORTED, HAL_OSPIM_IOPORT_1_LOW, HAL_OSPIM_IOPORT_NONE };
typedef struct { volatile uint32_t SR, DCR2, CR; } OCTOSPI_TypeDef;
static OCTOSPI_TypeDef octospi;
#define OCTOSPI1 (&octospi)
#define OCTOSPI_SR_BUSY 0x20u
#define OCTOSPI_DCR2_PRESCALER 0xFFu
#define MODIFY_REG(reg, clear, set) ((reg) = ((reg) & ~(clear)) | (set))
typedef struct {
    OCTOSPI_TypeDef* Instance;
    struct { uint32_t FifoThreshold, DualQuad, MemoryType, DeviceSize, ChipSelectHighTime, FreeRunningClock,
             ClockMode, WrapSize, ClockPrescaler, SampleShifting, DelayHoldQuarterCycle, ChipSelectBoundary,
             DelayBlockBypass, MaxTran, Refresh; } Init;
    uint32_t State;
} OSPI_HandleTypeDef;
typedef struct { uint32_t OperationType, FlashId, Instruction, InstructionMode, InstructionSize,
                 InstructionDtrMode, Address, AddressMode, AddressSize, AlternateBytesMode, DataMode, NbData,
                 DummyCycles, DQSMode, SIOOMode; } OSPI_RegularCmdTypeDef;
typedef struct { uint32_t ClkPort, NCSPort, IOLowPort, IOHighPort, Req2AckTime; } OSPIM_CfgTypeDef;
typedef struct { uint32_t TimeOutActivation, TimeOutPeriod; } OSPI_MemoryMappedTypeDef;

/* ---- the model ---- */
static bool chip_fitted, chip_qpi;          /* APS6404L: mode survives everything but a reset command */
static bool reset_armed, id_pending;
static int hal_calls, fail_call;            /* fail_call: 1-based index of the controller call that fails, 0 = none */
static int write_dqs = -1, mapped, timeout_on;
static uint32_t id_prescaler;

static int controller(void) { return ++hal_calls == fail_call ? HAL_ERROR : HAL_OK; }
static int HAL_OSPI_Init(OSPI_HandleTypeDef* h) { h->Instance->DCR2 = h->Init.ClockPrescaler - 1u; mapped = 0; return controller(); }
static void HAL_OSPI_SetTimeout(OSPI_HandleTypeDef* h, uint32_t t) { (void)h; assert(t <= 100u); }
static int HAL_OSPIM_Config(OSPI_HandleTypeDef* h, OSPIM_CfgTypeDef* c, uint32_t t) { (void)h; (void)c; assert(t <= 100u); return controller(); }
static int HAL_OSPI_Command(OSPI_HandleTypeDef* h, OSPI_RegularCmdTypeDef* c, uint32_t t)
{
    assert(t <= 100u);                      /* the 5 s HAL default must not come back */
    if (controller() != HAL_OK) return HAL_ERROR;
    if (c->OperationType == HAL_OSPI_OPTYPE_WRITE_CFG) { write_dqs = (int)c->DQSMode; return HAL_OK; }
    if (c->OperationType == HAL_OSPI_OPTYPE_READ_CFG) return HAL_OK;
    const bool four = c->InstructionMode == HAL_OSPI_INSTRUCTION_4_LINES;
    if (!chip_fitted || four != chip_qpi) { reset_armed = false; return HAL_OK; }   /* chip does not understand it */
    switch (c->Instruction) {
        case 0x66: reset_armed = true; return HAL_OK;
        case 0x99: if (reset_armed) chip_qpi = false; break;
        case 0xF5: chip_qpi = false; break;
        case 0x35: chip_qpi = true; break;
        case 0x9F: id_pending = true; id_prescaler = h->Instance->DCR2 + 1u; break;
        default: break;
    }
    reset_armed = false;
    return HAL_OK;
}
static int HAL_OSPI_Receive(OSPI_HandleTypeDef* h, uint8_t* data, uint32_t t)
{
    (void)h; assert(t <= 100u);
    if (controller() != HAL_OK) return HAL_ERROR;
    data[0] = id_pending ? 0x0D : 0x00;
    data[1] = id_pending ? 0x5D : 0x00;
    id_pending = false;
    return HAL_OK;
}
static int HAL_OSPI_MemoryMapped(OSPI_HandleTypeDef* h, OSPI_MemoryMappedTypeDef* m)
{
    (void)h; timeout_on = m->TimeOutActivation == HAL_OSPI_TIMEOUT_COUNTER_ENABLE && m->TimeOutPeriod > 0u;
    if (controller() != HAL_OK) return HAL_ERROR;
    mapped = 1; return HAL_OK;
}
#define __HAL_OSPI_DISABLE_IT(h, it) ((void)(h))
#define __HAL_RCC_GPIOB_CLK_ENABLE()
#define __HAL_RCC_GPIOD_CLK_ENABLE()
#define __HAL_RCC_OCTOSPIM_CLK_ENABLE()
#define __HAL_RCC_OSPI1_CLK_ENABLE()
#define __HAL_RCC_OSPI1_CLK_DISABLE()
#define __HAL_RCC_OSPI1_FORCE_RESET()
#define __HAL_RCC_OSPI1_RELEASE_RESET()

typedef struct { uint32_t Enable, Number, BaseAddress, Size, AccessPermission, TypeExtField, IsCacheable,
                 IsBufferable, IsShareable, DisableExec, SubRegionDisable; } MPU_Region_InitTypeDef;
enum { MPU_REGION_ENABLE, MPU_REGION_NUMBER0, MPU_REGION_SIZE_8MB, MPU_REGION_FULL_ACCESS, MPU_TEX_LEVEL0,
       MPU_TEX_LEVEL1, MPU_ACCESS_CACHEABLE, MPU_ACCESS_NOT_CACHEABLE, MPU_ACCESS_BUFFERABLE,
       MPU_ACCESS_SHAREABLE, MPU_ACCESS_NOT_SHAREABLE, MPU_INSTRUCTION_ACCESS_DISABLE, MPU_PRIVILEGED_DEFAULT };
static void HAL_MPU_Disable(void) {}
static void HAL_MPU_Enable(uint32_t m) { (void)m; }
static void HAL_MPU_ConfigRegion(MPU_Region_InitTypeDef* r) { (void)r; }
static unsigned cache_ops;
static uint32_t* window;
static void SCB_CleanDCache_by_Addr(void* a, int32_t n) { assert(n > 0 && n % 32 == 0 && ((uintptr_t)a - (uintptr_t)window) % 32u == 0); cache_ops++; }
static void SCB_InvalidateDCache_by_Addr(void* a, int32_t n) { assert(n > 0 && n % 32 == 0 && ((uintptr_t)a - (uintptr_t)window) % 32u == 0); cache_ops++; }
static struct { uint32_t ACTLR; } scnscb;
#define SCnSCB (&scnscb)
#define SCnSCB_ACTLR_DISRAMODE_Msk 0x800u
#define __DSB()
#define __ISB()

#define RCC_RSR_PINRSTF 0x00400000u
#define RCC_RSR_PORRSTF 0x00800000u
#define RCC_RSR_SFTRSTF 0x01000000u
#define RCC_RSR_IWDG1RSTF 0x04000000u
static uint32_t reset_flags;
static uint32_t ufi_crash_reset_flags(void) { return reset_flags; }
static const uint32_t dma_detail_model[7] = {0, 1, 0, 300, 0, 111, 0};       /* one guard after zeroed; 300 saturates */
const uint32_t* ufi_flux_dma_detail(void) { return dma_detail_model; }


#define OCTOSPI1_BASE ((uintptr_t)window)
#include "../src/ufi_psram.c"

static void board(bool fitted, bool qpi, uint32_t flags)
{
    chip_fitted = fitted; chip_qpi = qpi; reset_armed = id_pending = false;
    hal_calls = fail_call = 0; write_dqs = -1; mapped = timeout_on = 0; id_prescaler = 0;
    reset_flags = flags; skip_reason = NULL; cache_ops = 0;
}
static bool has(const char* text) { return strstr(ufi_psram_result(), text) != NULL; }
'''

MAIN = r'''
int main(void)
{
    uint8_t s[32];
    window = calloc(PSRAM_SIZE_BYTES / 4u, 4u);
    assert(window);

    /* cold start, chip in SPI mode */
    board(true, false, RCC_RSR_PORRSTF | RCC_RSR_PINRSTF); psram_trial = 0;
    assert(ufi_psram_init() == UFI_OK && has("PSRAM 8 MB ok") && psram_trial == 0);
    assert(chip_qpi && mapped && timeout_on);
    assert(write_dqs == HAL_OSPI_DQS_ENABLE);           /* erratum ES0491 2.8.6 */
    assert(id_prescaler == OSPI_PROBE_PRESCALER && 275u / id_prescaler <= 33u);    /* READ ID <= 33 MHz */
    assert(octospi.DCR2 == OSPI_PRESCALER - 1u);
    assert(cache_ops > 0);                              /* by address, whole lines (asserted in the stub) */
    assert(scnscb.ACTLR & SCnSCB_ACTLR_DISRAMODE_Msk);   /* no stores past the cache (bench 2026-11-12) */
    const int calls_ok = hal_calls;
    assert(ufi_psram_status_pack(s) == 32 && s[0] == 1 && s[1] == UFI_PSRAM_STATE_OK &&
           s[2] == UFI_PSRAM_PH_LINES && s[3] == 0 && s[4] == 0x0D && s[5] == 0x5D && s[8] == 0);
    ufi_psram_dma_result(0);
    ufi_psram_status_pack(s); assert(s[1] == UFI_PSRAM_STATE_OK && s[2] == UFI_PSRAM_PH_DMA);
    ufi_psram_dma_result(UFI_DMA_TEST_NOT_RUN);
    ufi_psram_status_pack(s); assert(s[1] == UFI_PSRAM_STATE_DMA_NOT_RUN && has("DMA test did not run"));
    ufi_psram_dma_result((2u << 8) | 3u);
    ufi_psram_status_pack(s);
    assert(s[1] == UFI_PSRAM_STATE_FAILED && s[2] == UFI_PSRAM_PH_DMA && s[20] == 3 && s[21] == 2 &&
           has("DMA writes unsafe (run 2 code 3: b0 a1 d0 z300 f0 w[111]=00000000)") &&
           s[28] == 0 && s[29] == 1 && s[30] == 0 && s[31] == 255);

    /* bench fault 1: warm start, the chip is still in QPI mode */
    board(true, true, RCC_RSR_SFTRSTF | RCC_RSR_PINRSTF);
    assert(ufi_psram_init() == UFI_OK && has("PSRAM 8 MB ok") && chip_qpi);

    /* no chip: both IDs in text and block, marker cleared, board would come up */
    board(false, false, RCC_RSR_PORRSTF);
    assert(ufi_psram_init() == UFI_ERR_NOT_IMPL && has("PSRAM not answering (id 00 00) gpio FF FF"));
    assert(psram_trial == 0 && !mapped);
    ufi_psram_status_pack(s);
    assert(s[1] == UFI_PSRAM_STATE_FAILED && s[2] == UFI_PSRAM_PH_ID && s[6] == 0xFF && s[7] == 0xFF && !(s[8] & 1));

    /* every controller call fails once: named phase, timeout flag, marker cleared, no window */
    unsigned by_phase[16] = {0};
    for (int k = 1; k <= calls_ok; k++) {
        board(true, false, RCC_RSR_PORRSTF); fail_call = k;
        const int r = ufi_psram_init();
        assert(r == UFI_ERR_TIMEOUT && psram_trial == 0 && !mapped);
        ufi_psram_status_pack(s);
        assert(s[1] == UFI_PSRAM_STATE_FAILED && (s[8] & 1) && s[2] >= 1 && s[2] <= UFI_PSRAM_PH_MAP);
        assert(s[2] == UFI_PSRAM_PH_ID ? has("t/o") : has("controller timeout"));
        assert(s[2] == UFI_PSRAM_PH_ID || has(phase_name[s[2]]));
        by_phase[s[2]]++;
    }
    for (unsigned p = UFI_PSRAM_PH_CONTROLLER; p <= UFI_PSRAM_PH_MAP; p++) assert(by_phase[p] > 0);
    assert(by_phase[UFI_PSRAM_PH_QPI_RESET] == 3 && by_phase[UFI_PSRAM_PH_SPI_RESET] == 2);   /* all five reset commands */

    /* a start that ended in "first write": latched off, whatever reset follows, until power-on */
    const struct { uint32_t flags; const char* how; } ended[] = {
        {RCC_RSR_SFTRSTF | RCC_RSR_PINRSTF, "(software reset)"}, {RCC_RSR_IWDG1RSTF | RCC_RSR_PINRSTF, "(watchdog)"},
        {RCC_RSR_PINRSTF, "(pin reset)"},
    };
    for (unsigned i = 0; i < 3; i++) {
        board(true, true, ended[i].flags); psram_trial = TRIAL_MAGIC | UFI_PSRAM_PH_FIRST_WRITE;
        assert(ufi_psram_init() == UFI_ERR_NOT_IMPL && hal_calls == 0);            /* chip and controller untouched */
        assert(has("PSRAM off: last start ended in phase 8 first write") && has(ended[i].how));
        assert(psram_trial == (LATCH_MAGIC | UFI_PSRAM_PH_FIRST_WRITE));
        ufi_psram_status_pack(s); assert(s[1] == UFI_PSRAM_STATE_LATCHED && s[3] == UFI_PSRAM_PH_FIRST_WRITE);
        board(true, true, RCC_RSR_SFTRSTF);                                        /* e.g. a firmware flash */
        assert(ufi_psram_init() == UFI_ERR_NOT_IMPL && hal_calls == 0 && has("PSRAM off since a start ended in phase 8"));
        assert(psram_trial == (LATCH_MAGIC | UFI_PSRAM_PH_FIRST_WRITE));
    }
    board(true, true, RCC_RSR_PORRSTF | RCC_RSR_PINRSTF);                          /* power cycle lifts the latch */
    assert(ufi_psram_init() == UFI_OK && psram_trial == 0);

    /* host retest lifts the latch; a marker with another magic is not a latch */
    board(true, true, RCC_RSR_SFTRSTF); psram_trial = LATCH_MAGIC | UFI_PSRAM_PH_NARROW;
    assert(ufi_psram_init() == UFI_ERR_NOT_IMPL);
    ufi_psram_unlatch();
    assert(ufi_psram_init() == UFI_OK && has("PSRAM 8 MB ok"));
    board(true, false, RCC_RSR_SFTRSTF); psram_trial = 0xDEADBEEFu;                /* stack paint of an old image */
    assert(ufi_psram_init() == UFI_OK);

    /* safe start: nothing touched, a latch stays as it is, retest lifts the skip */
    board(true, false, RCC_RSR_PINRSTF); psram_trial = LATCH_MAGIC | UFI_PSRAM_PH_MAP;
    ufi_psram_skip("PSRAM skipped: safe start (button B)");
    assert(ufi_psram_init() == UFI_ERR_NOT_IMPL && hal_calls == 0 && has("safe start"));
    assert(psram_trial == (LATCH_MAGIC | UFI_PSRAM_PH_MAP));
    ufi_psram_status_pack(s); assert(s[1] == UFI_PSRAM_STATE_SAFE_START);
    ufi_psram_unlatch();
    assert(ufi_psram_init() == UFI_OK);

    free(window);
    puts("psram start-up: QPI warm start, controller failure in every phase, silent chip, latch by reset kind, "
         "power cycle, retest, safe start, status block passed");
    return 0;
}
'''


def main():
    run('regression_psram_init', BASE + ERRORS + STUB + MAIN)


if __name__ == '__main__':
    main()
