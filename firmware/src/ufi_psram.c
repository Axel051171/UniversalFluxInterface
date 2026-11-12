/**
 * UFI Flux Engine - QSPI PSRAM (APS6404L, 8 MB) on OCTOSPI1
 *
 * Init in SPI mode (reset, read ID), switch the PSRAM to QPI, then map it at
 * OCTOSPI1_BASE (0x90000000) for reads and writes, so CPU and DMA use it like RAM.
 *   clock  : HCLK 275 MHz / 8 = 34.4 MHz (bring-up value: half a period of 14.5 ns for
 *            tACLK without sample shifting; 68.75 MHz needs SSHIFT proven on the bench)
 *   CSBOUND: 1 KB page (linear bursts must not cross a page)
 *   REFRESH: CS# released at least every 8 us (tCEM); TIMEOUT releases it after the
 *            read prefetch stalls, otherwise CS# would stay low and block the refresh
 *
 * First bench run on v1.0 (2026-11-11), three faults in this file:
 *   1. A chip left in QPI mode (warm reset) was never brought back: fixed in the init.
 *   2. READ ID ran at 68.75 MHz, the datasheet allows 33 MHz: probe clock.
 *   3. STM32H72x/73x erratum "Memory-mapped write error response when DQS output is
 *      disabled" (ES0491 2.8.6): with DQSE clear in WCCR every write through the window
 *      is answered with a bus error, the fault handler resets, the start-up loops. DQSE
 *      is now set for the write command although the chip has no DQS pin.
 * Still open, and the reason for the narrow-write test below: reports for exactly this
 * pair (H723 + APS6404L, quad SDR) say the controller pads every write narrower than
 * 64 bit with zeros, which destroys the neighbouring word. A DMA capture writes 32-bit
 * words, so the store is only handed out when 32-bit writes are proven exact.
 */

#include "ufi_firmware.h"
#include "ufi_psram.h"
#include <stdio.h>
#include <string.h>

#if BOARD_HAS_PSRAM

static OSPI_HandleTypeDef hospi;
#if defined(UFI_FORCE_INTERNAL_FLUX_STORE)
static const char* psram_result = "PSRAM bypassed (qualification)";
#else
static const char* psram_result = "PSRAM init failed";
#endif

#define PSRAM_CMD_RESET_EN      0x66
#define PSRAM_CMD_RESET         0x99
#define PSRAM_CMD_READ_ID       0x9F
#define PSRAM_CMD_ENTER_QPI     0x35
#define PSRAM_CMD_EXIT_QPI      0xF5    /* QPI only */
#define PSRAM_CMD_QUAD_READ     0xEB    /* QPI: 6 wait cycles */
#define PSRAM_CMD_QUAD_WRITE    0x38
#define PSRAM_QPI_READ_WAIT     6
#define PSRAM_ID_MF             0x0D    /* AP Memory */
#define PSRAM_ID_KGD            0x5D    /* known good die */

#define OSPI_PRESCALER          8
#define OSPI_PROBE_PRESCALER    16      /* 17.2 MHz: READ ID 0x9F is limited to 33 MHz (datasheet 8.5) */
#define OSPI_CLK_HZ             (275000000UL / OSPI_PRESCALER)
#define PSRAM_TCEM_CYCLES       ((OSPI_CLK_HZ / 1000000UL) * 8UL)   /* 8 us */
#define PSRAM_IDLE_CYCLES       16u     /* CS# released this long after the prefetch stalls */
/* Every controller wait in this file. A command takes microseconds; the HAL default of
 * 5 s per call would add up to more than the watchdog period on a dead controller. */
#define PSRAM_HAL_TIMEOUT_MS    100u

/* Start-up trial marker. The first accesses through the window can kill the start-up
 * (bus fault -> reset, or a stalled bus). DTCM keeps its contents through a reset, so the
 * step entered last tells the next start where the previous one died; that start leaves
 * the PSRAM alone and the board comes up on the internal store instead of looping. */
#define TRIAL_MAGIC             0x50535200u     /* "PSR": start-up in progress, phase in the low byte */
#define LATCH_MAGIC             0x504F4600u     /* "POF": PSRAM off after a dead start, phase in the low byte */
/* volatile: each step must really be stored; for the compiler all but the last are dead */
__attribute__((section(".dtcm.psram_trial"), aligned(4))) static volatile uint32_t psram_trial;

static void trial(uint32_t step)
{
    psram_trial = TRIAL_MAGIC | step;
}

static void psram_gpio_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    g.Alternate = GPIO_AF9_OCTOSPIM_P1;
    g.Pin = PSRAM_CLK_PIN | PSRAM_NCS_PIN;
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = PSRAM_IO0_PIN | PSRAM_IO1_PIN | PSRAM_IO3_PIN;
    HAL_GPIO_Init(GPIOD, &g);
    g.Alternate = GPIO_AF4_OCTOSPIM_P1;
    g.Pin = PSRAM_IO2_PIN;
    HAL_GPIO_Init(PSRAM_IO2_PORT, &g);
}

/* ---- bring-up aid: READ ID by GPIO bit-banging (SPI mode 0), failure path only ---- */

static void probe_wait(void)
{
    for (volatile uint32_t i = 0; i < 40u; i++) {       /* >= 100 ns per half period */
    }
}

static void probe_tx(uint8_t byte)
{
    for (int bit = 7; bit >= 0; bit--) {
        HAL_GPIO_WritePin(PSRAM_IO0_PORT, PSRAM_IO0_PIN, ((byte >> bit) & 1u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        probe_wait();
        HAL_GPIO_WritePin(PSRAM_CLK_PORT, PSRAM_CLK_PIN, GPIO_PIN_SET);
        probe_wait();
        HAL_GPIO_WritePin(PSRAM_CLK_PORT, PSRAM_CLK_PIN, GPIO_PIN_RESET);
    }
}

static uint8_t probe_rx(void)
{
    uint8_t byte = 0;
    for (int bit = 0; bit < 8; bit++) {
        probe_wait();
        byte = (uint8_t)((byte << 1) | (HAL_GPIO_ReadPin(PSRAM_IO1_PORT, PSRAM_IO1_PIN) == GPIO_PIN_SET ? 1u : 0u));
        HAL_GPIO_WritePin(PSRAM_CLK_PORT, PSRAM_CLK_PIN, GPIO_PIN_SET);
        probe_wait();
        HAL_GPIO_WritePin(PSRAM_CLK_PORT, PSRAM_CLK_PIN, GPIO_PIN_RESET);
    }
    return byte;
}

static void probe_command(uint8_t instr)
{
    HAL_GPIO_WritePin(PSRAM_NCS_PORT, PSRAM_NCS_PIN, GPIO_PIN_RESET);
    probe_tx(instr);
    HAL_GPIO_WritePin(PSRAM_NCS_PORT, PSRAM_NCS_PIN, GPIO_PIN_SET);
    probe_wait();
}

/* Takes the pins away from OCTOSPI (the PSRAM stays unused afterwards) */
static void psram_gpio_probe(uint8_t id[2])
{
    __HAL_RCC_OSPI1_CLK_DISABLE();
    HAL_GPIO_WritePin(PSRAM_NCS_PORT, PSRAM_NCS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PSRAM_CLK_PORT, PSRAM_CLK_PIN, GPIO_PIN_RESET);
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Pin = PSRAM_CLK_PIN | PSRAM_NCS_PIN;
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = PSRAM_IO0_PIN;
    HAL_GPIO_Init(PSRAM_IO0_PORT, &g);
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    g.Pin = PSRAM_IO1_PIN;
    HAL_GPIO_Init(PSRAM_IO1_PORT, &g);
    g.Pin = PSRAM_IO3_PIN;                              /* SIO2/SIO3 unused in SPI mode: held high */
    HAL_GPIO_Init(PSRAM_IO3_PORT, &g);
    g.Pin = PSRAM_IO2_PIN;
    HAL_GPIO_Init(PSRAM_IO2_PORT, &g);
    HAL_Delay(1);

    probe_command(PSRAM_CMD_RESET_EN);
    probe_command(PSRAM_CMD_RESET);
    HAL_Delay(1);

    HAL_GPIO_WritePin(PSRAM_NCS_PORT, PSRAM_NCS_PIN, GPIO_PIN_RESET);
    probe_tx(PSRAM_CMD_READ_ID);
    probe_tx(0);
    probe_tx(0);
    probe_tx(0);
    id[0] = probe_rx();
    id[1] = probe_rx();
    HAL_GPIO_WritePin(PSRAM_NCS_PORT, PSRAM_NCS_PIN, GPIO_PIN_SET);
}

/* Single-line command without address/data (SPI mode) or 4-line (QPI) */
static int psram_cmd(uint8_t instr, bool qpi)
{
    OSPI_RegularCmdTypeDef c = {0};
    c.OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;
    c.FlashId = HAL_OSPI_FLASH_ID_1;
    c.Instruction = instr;
    c.InstructionMode = qpi ? HAL_OSPI_INSTRUCTION_4_LINES : HAL_OSPI_INSTRUCTION_1_LINE;
    c.InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;
    c.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
    c.AddressMode = HAL_OSPI_ADDRESS_NONE;
    c.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    c.DataMode = HAL_OSPI_DATA_NONE;
    c.DummyCycles = 0;
    c.DQSMode = HAL_OSPI_DQS_DISABLE;
    c.SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;
    return HAL_OSPI_Command(&hospi, &c, PSRAM_HAL_TIMEOUT_MS) == HAL_OK ? UFI_OK : UFI_ERR_TIMEOUT;
}

static int psram_read_id(uint8_t id[2])
{
    OSPI_RegularCmdTypeDef c = {0};
    c.OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;
    c.FlashId = HAL_OSPI_FLASH_ID_1;
    c.Instruction = PSRAM_CMD_READ_ID;
    c.InstructionMode = HAL_OSPI_INSTRUCTION_1_LINE;
    c.InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;
    c.Address = 0;
    c.AddressMode = HAL_OSPI_ADDRESS_1_LINE;
    c.AddressSize = HAL_OSPI_ADDRESS_24_BITS;
    c.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    c.DataMode = HAL_OSPI_DATA_1_LINE;
    c.NbData = 2;
    c.DummyCycles = 0;
    c.DQSMode = HAL_OSPI_DQS_DISABLE;
    c.SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;
    if (HAL_OSPI_Command(&hospi, &c, PSRAM_HAL_TIMEOUT_MS) != HAL_OK ||
        HAL_OSPI_Receive(&hospi, id, PSRAM_HAL_TIMEOUT_MS) != HAL_OK) {
        return UFI_ERR_TIMEOUT;
    }
    return UFI_OK;
}

static int psram_memory_mapped(void)
{
    OSPI_RegularCmdTypeDef c = {0};
    c.FlashId = HAL_OSPI_FLASH_ID_1;
    c.InstructionMode = HAL_OSPI_INSTRUCTION_4_LINES;
    c.InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;
    c.AddressMode = HAL_OSPI_ADDRESS_4_LINES;
    c.AddressSize = HAL_OSPI_ADDRESS_24_BITS;
    c.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    c.DataMode = HAL_OSPI_DATA_4_LINES;
    c.DQSMode = HAL_OSPI_DQS_DISABLE;
    c.SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;

    c.OperationType = HAL_OSPI_OPTYPE_READ_CFG;
    c.Instruction = PSRAM_CMD_QUAD_READ;
    c.DummyCycles = PSRAM_QPI_READ_WAIT;
    if (HAL_OSPI_Command(&hospi, &c, PSRAM_HAL_TIMEOUT_MS) != HAL_OK) {
        return UFI_ERR_TIMEOUT;
    }
    c.OperationType = HAL_OSPI_OPTYPE_WRITE_CFG;
    c.Instruction = PSRAM_CMD_QUAD_WRITE;
    c.DummyCycles = 0;
    c.DQSMode = HAL_OSPI_DQS_ENABLE;            /* erratum ES0491 2.8.6, see the file header */
    if (HAL_OSPI_Command(&hospi, &c, PSRAM_HAL_TIMEOUT_MS) != HAL_OK) {
        return UFI_ERR_TIMEOUT;
    }

    OSPI_MemoryMappedTypeDef mm = {0};
    mm.TimeOutActivation = HAL_OSPI_TIMEOUT_COUNTER_ENABLE;
    mm.TimeOutPeriod = PSRAM_IDLE_CYCLES;
    if (HAL_OSPI_MemoryMapped(&hospi, &mm) != HAL_OK) {
        return UFI_ERR_TIMEOUT;
    }
    __HAL_OSPI_DISABLE_IT(&hospi, HAL_OSPI_IT_TO);  /* the HAL enables it; nobody handles it */
    return UFI_OK;
}

/* MPU region for the mapped window. Cacheable: normal memory, write-back (the working
 * setting; the CPU then writes whole cache lines). Otherwise device memory: every access
 * goes to the bus exactly as coded, which is how a DMA write arrives. */
static void psram_mpu(bool cacheable)
{
    HAL_MPU_Disable();
    MPU_Region_InitTypeDef r = {0};
    r.Enable = MPU_REGION_ENABLE;
    r.Number = MPU_REGION_NUMBER0;
    r.BaseAddress = OCTOSPI1_BASE;
    r.Size = MPU_REGION_SIZE_8MB;
    r.AccessPermission = MPU_REGION_FULL_ACCESS;
    r.TypeExtField = cacheable ? MPU_TEX_LEVEL1 : MPU_TEX_LEVEL0;
    r.IsCacheable = cacheable ? MPU_ACCESS_CACHEABLE : MPU_ACCESS_NOT_CACHEABLE;
    r.IsBufferable = MPU_ACCESS_BUFFERABLE;
    r.IsShareable = cacheable ? MPU_ACCESS_NOT_SHAREABLE : MPU_ACCESS_SHAREABLE;
    r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.SubRegionDisable = 0;
    HAL_MPU_ConfigRegion(&r);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/* ---- narrow-write test (device memory, single 32-bit bus accesses) --------------------
 * Patterns without a zero byte, so a write padded with zeros is told from a write that
 * left the neighbour alone. */
#define NARROW_WORDS    64u
#define NARROW_GUARD    8u

typedef struct {
    uint32_t fill;      /* ascending fill read back wrong */
    uint32_t asc;       /* ascending overwrite read back wrong */
    uint32_t desc;      /* descending overwrite read back wrong */
    uint32_t lone;      /* two single writes: any other word changed */
    uint32_t tail;      /* word after an ascending run: 0 untouched, 1 zeroed, 2 other */
} narrow_t;

static uint32_t pat_s(uint32_t i)
{
    return 0x81818181u + i * 0x01010101u;
}

static uint32_t pat_p(uint32_t i)
{
    return ~pat_s(i);
}

static void narrow_fill(volatile uint32_t* m)
{
    for (uint32_t i = 0; i < NARROW_WORDS + NARROW_GUARD; i++) {
        m[i] = pat_s(i);
    }
}

static void psram_narrow_test(volatile uint32_t* m, narrow_t* r)
{
    const uint32_t n = NARROW_WORDS, all = NARROW_WORDS + NARROW_GUARD;

    narrow_fill(m);
    for (uint32_t i = 0; i < all; i++) {
        r->fill += m[i] != pat_s(i);
    }

    for (uint32_t i = 0; i < n; i++) {
        m[i] = pat_p(i);
    }
    for (uint32_t i = 0; i < n; i++) {
        r->asc += m[i] != pat_p(i);
    }
    const uint32_t after = m[n];
    const uint32_t tail = after == pat_s(n) ? 0u : after == 0u ? 1u : 2u;
    if (tail > r->tail) {
        r->tail = tail;
    }

    narrow_fill(m);
    for (uint32_t i = n; i-- > 0u;) {
        m[i] = pat_p(i);
    }
    for (uint32_t i = 0; i < n; i++) {
        r->desc += m[i] != pat_p(i);
    }

    narrow_fill(m);
    m[11] = pat_p(11);
    m[20] = pat_p(20);
    for (uint32_t i = 0; i < all; i++) {
        r->lone += m[i] != ((i == 11u || i == 20u) ? pat_p(i) : pat_s(i));
    }
}

/* ---- line test (cacheable): data lines (walking one), address lines (unique value at
 * every power-of-two word offset, catches shorted/open address bits), pattern in each MB.
 * Returns NULL or the fault text. */
/* Writes back and drops the one cache line that holds *p: the test must read from the
 * chip, and it must not disturb the rest of the cache (it used to clean the whole cache
 * 34 times). */
static void line_flush(volatile uint32_t* p)
{
    uint32_t* line = (uint32_t*)((uintptr_t)p & ~(uintptr_t)31u);
    SCB_CleanDCache_by_Addr(line, 32);
    SCB_InvalidateDCache_by_Addr(line, 32);
}

/* Long sequential CPU writes (the pattern that makes the core stream stores past the
 * cache): first whole lines, then a run that starts and ends inside a doubleword, each
 * read back from the chip together with the words around it. Returns NULL or a text. */
#define STREAM_FIRST    256u            /* word index, line aligned */
#define STREAM_WORDS    1056u           /* 132 lines */
#define STREAM_INNER    3u              /* inner run starts 3 words in ... */
#define STREAM_INNER_N  1027u           /* ... and has an odd length */

static void region_flush(volatile uint32_t* p, uint32_t words)
{
    SCB_CleanDCache_by_Addr((uint32_t*)(uintptr_t)p, (int32_t)(words * 4u));
    SCB_InvalidateDCache_by_Addr((uint32_t*)(uintptr_t)p, (int32_t)(words * 4u));
}

static const char* psram_stream_test(volatile uint32_t* mem)
{
    static char text[64];
    volatile uint32_t* r = mem + STREAM_FIRST;
    for (uint32_t i = 0; i < STREAM_WORDS; i++) {
        r[i] = pat_s(i & 0x3Fu);
    }
    region_flush(r, STREAM_WORDS);
    for (uint32_t i = 0; i < STREAM_WORDS; i++) {
        if (r[i] != pat_s(i & 0x3Fu)) {
            snprintf(text, sizeof(text), "PSRAM cache write fault (fill w[%lu]=%08lX)",
                     (unsigned long)i, (unsigned long)r[i]);
            return text;
        }
    }
    for (uint32_t i = STREAM_INNER; i < STREAM_INNER + STREAM_INNER_N; i++) {
        r[i] = pat_p(i & 0x3Fu);
    }
    region_flush(r, STREAM_WORDS);
    for (uint32_t i = 0; i < STREAM_WORDS; i++) {
        const bool inner = i >= STREAM_INNER && i < STREAM_INNER + STREAM_INNER_N;
        if (r[i] != (inner ? pat_p(i & 0x3Fu) : pat_s(i & 0x3Fu))) {
            snprintf(text, sizeof(text), "PSRAM cache write fault (run w[%lu]=%08lX)",
                     (unsigned long)i, (unsigned long)r[i]);
            return text;
        }
    }
    return NULL;
}

static const char* psram_line_test(volatile uint32_t* mem)
{
    const uint32_t words = PSRAM_SIZE_BYTES / 4u;
    for (uint32_t bit = 1; bit != 0; bit <<= 1) {
        mem[0] = bit;
        line_flush(&mem[0]);
        if (mem[0] != bit) {
            return "PSRAM data line fault";
        }
    }
    mem[0] = 0x5555AAAAu;
    for (uint32_t a = 1; a < words; a <<= 1) {
        mem[a] = 0xC0DE0000u ^ a;
    }
    line_flush(&mem[0]);
    for (uint32_t a = 1; a < words; a <<= 1) {
        line_flush(&mem[a]);
    }
    if (mem[0] != 0x5555AAAAu) {
        return "PSRAM address line fault";
    }
    for (uint32_t a = 1; a < words; a <<= 1) {
        if (mem[a] != (0xC0DE0000u ^ a)) {
            return "PSRAM address line fault";
        }
    }
    for (uint32_t a = 0; a < words; a += words / 8u) {
        mem[a + 3] = 0xA5A50000u ^ a;       /* words 3 and 4 share one 32-byte line */
        mem[a + 4] = ~(0xA5A50000u ^ a);
    }
    for (uint32_t a = 0; a < words; a += words / 8u) {
        line_flush(&mem[a + 3]);
    }
    for (uint32_t a = 0; a < words; a += words / 8u) {
        if (mem[a + 3] != (0xA5A50000u ^ a) || mem[a + 4] != ~(0xA5A50000u ^ a)) {
            return "PSRAM pattern fault";
        }
    }
    return NULL;
}

/* Returns UFI_OK when the PSRAM answered with the right ID and passed a pattern test */
/* ---- status of the last start-up, also as structured data (SELFTEST 'P') -------------- */

static const char* const phase_name[] = {
    "", "controller", "QPI reset", "SPI reset", "read ID", "enter QPI", "map window",
    "first read", "first write", "32-bit writes", "cache lines", "DMA writes",
};

static struct {
    uint8_t state;              /* UFI_PSRAM_STATE_* */
    uint8_t phase;              /* phase that failed, or the last one passed */
    uint8_t latched_phase;      /* phase a dead start did not get past, 0 = none */
    uint8_t id[2], gpio_id[2];
    uint8_t flags;              /* bit0 controller timeout, bit1 cache line test failed */
    narrow_t narrow;
    uint32_t dma;
    uint32_t dma_detail[5];     /* ufi_flux_dma_detail() of a failed DMA test */
} diag;

static char status_text[96];

/* A phase failed without killing the start: text, structured data, marker cleared */
static int phase_failed(uint8_t phase, bool controller_timeout, const char* text)
{
    diag.state = UFI_PSRAM_STATE_FAILED;
    diag.phase = phase;
    if (controller_timeout) {
        diag.flags |= 1u;
    }
    if (text == NULL) {
        /* "sent" is all the controller can say: a command it clocked out is not a command
         * the chip took. A timeout here is the controller itself, not the PSRAM. */
        snprintf(status_text, sizeof(status_text), "PSRAM init: %s, controller timeout", phase_name[phase]);
        text = status_text;
    }
    psram_result = text;
    psram_trial = 0;
    return controller_timeout ? UFI_ERR_TIMEOUT : UFI_ERR_NOT_IMPL;
}

/* Requested before ufi_flux_init() (safe start): the next ufi_psram_init() touches neither
 * the controller nor the chip and reports the reason. */
static const char* skip_reason;

void ufi_psram_skip(const char* why)
{
    skip_reason = why;
}

/* Host request "test the PSRAM again": lifts a safe-start skip and the latch */
void ufi_psram_unlatch(void)
{
    skip_reason = NULL;
    psram_trial = 0;
}

/* A marker left by an earlier start. Returns true when the PSRAM stays off.
 * Policy (owner default 2026-11-11): latched off until a power-on reset or a host retest,
 * so a board with a PSRAM fault stays reachable through every later reset. */
static bool psram_latched(void)
{
    const uint32_t mark = psram_trial;
    const bool fresh = (mark & ~0xFFu) == TRIAL_MAGIC;
    if (!fresh && (mark & ~0xFFu) != LATCH_MAGIC) {
        return false;
    }
    const uint32_t rsr = ufi_crash_reset_flags();
    if (rsr & RCC_RSR_PORRSTF) {
        psram_trial = 0;                    /* power cycle: DTCM is stale, start afresh */
        return false;
    }
    const uint8_t phase = (uint8_t)(mark & 0xFFu);
    const char* name = phase < sizeof(phase_name) / sizeof(phase_name[0]) ? phase_name[phase] : "?";
    psram_trial = LATCH_MAGIC | phase;
    diag.state = UFI_PSRAM_STATE_LATCHED;
    diag.latched_phase = phase;
    if (fresh) {
        /* The reset that ended that start: a watchdog or software reset is the firmware
         * giving up (hang, fault handler); a pin reset is somebody pressing RESET or a
         * debugger, which says nothing about the PSRAM. */
        const char* how = (rsr & RCC_RSR_IWDG1RSTF) ? "watchdog" :
                          (rsr & RCC_RSR_SFTRSTF) ? "software reset" :
                          (rsr & RCC_RSR_PINRSTF) ? "pin reset" : "reset";
        snprintf(status_text, sizeof(status_text),
                 "PSRAM off: last start ended in phase %u %s (%s); power cycle or retest",
                 (unsigned)phase, name, how);
    } else {
        snprintf(status_text, sizeof(status_text),
                 "PSRAM off since a start ended in phase %u %s; power cycle or retest",
                 (unsigned)phase, name);
    }
    psram_result = status_text;
    return true;
}

int ufi_psram_init(void)
{
    memset(&diag, 0, sizeof(diag));
    if (skip_reason != NULL) {
        diag.state = UFI_PSRAM_STATE_SAFE_START;
        psram_result = skip_reason;         /* a deliberate skip leaves a latch as it is */
        return UFI_ERR_NOT_IMPL;
    }
    if (psram_latched()) {
        return UFI_ERR_NOT_IMPL;
    }

    trial(UFI_PSRAM_PH_CONTROLLER);
    __HAL_RCC_OCTOSPIM_CLK_ENABLE();
    __HAL_RCC_OSPI1_CLK_ENABLE();
    __HAL_RCC_OSPI1_FORCE_RESET();
    __HAL_RCC_OSPI1_RELEASE_RESET();
    psram_gpio_init();

    hospi.Instance = OCTOSPI1;
    hospi.Init.FifoThreshold = 4;
    hospi.Init.DualQuad = HAL_OSPI_DUALQUAD_DISABLE;
    hospi.Init.MemoryType = HAL_OSPI_MEMTYPE_APMEMORY;
    hospi.Init.DeviceSize = 23;                         /* 2^23 bytes */
    hospi.Init.ChipSelectHighTime = 2;                  /* tCPH >= 18 ns */
    hospi.Init.FreeRunningClock = HAL_OSPI_FREERUNCLK_DISABLE;
    hospi.Init.ClockMode = HAL_OSPI_CLOCK_MODE_0;
    hospi.Init.WrapSize = HAL_OSPI_WRAP_NOT_SUPPORTED;
    hospi.Init.ClockPrescaler = OSPI_PROBE_PRESCALER;   /* reset + READ ID, working clock below */
    hospi.Init.SampleShifting = HAL_OSPI_SAMPLE_SHIFTING_NONE;
    hospi.Init.DelayHoldQuarterCycle = HAL_OSPI_DHQC_DISABLE;
    hospi.Init.ChipSelectBoundary = 10;                 /* 1 KB page */
    hospi.Init.DelayBlockBypass = HAL_OSPI_DELAY_BLOCK_BYPASSED;
    hospi.Init.MaxTran = 0;
    hospi.Init.Refresh = PSRAM_TCEM_CYCLES - 16;
    hospi.State = HAL_OSPI_STATE_RESET;                 /* a retest starts the HAL handle afresh */
    if (HAL_OSPI_Init(&hospi) != HAL_OK) {
        return phase_failed(UFI_PSRAM_PH_CONTROLLER, true, NULL);
    }
    HAL_OSPI_SetTimeout(&hospi, PSRAM_HAL_TIMEOUT_MS);  /* waits inside the HAL (memory-mapped entry) */

    OSPIM_CfgTypeDef m = {0};
    m.ClkPort = 1;
    m.NCSPort = 1;
    m.IOLowPort = HAL_OSPIM_IOPORT_1_LOW;
    m.IOHighPort = HAL_OSPIM_IOPORT_NONE;
    m.Req2AckTime = 1;
    if (HAL_OSPIM_Config(&hospi, &m, PSRAM_HAL_TIMEOUT_MS) != HAL_OK) {
        return phase_failed(UFI_PSRAM_PH_CONTROLLER, true, NULL);
    }

    HAL_Delay(1);                                       /* tPU 150 us after power-up */
    /* The chip may sit in QPI mode: after a warm MCU reset (QPI survives it), and on the
     * v1.0 bench boards even after a power cycle. There READ ID does not exist and the
     * one-line reset is not understood, so the reset and "exit quad" go out on four lines
     * first; a chip in SPI mode sees two clocks of an unfinished command and ignores them. */
    trial(UFI_PSRAM_PH_QPI_RESET);
    if (psram_cmd(PSRAM_CMD_RESET_EN, true) != UFI_OK || psram_cmd(PSRAM_CMD_RESET, true) != UFI_OK) {
        return phase_failed(UFI_PSRAM_PH_QPI_RESET, true, NULL);
    }
    HAL_Delay(1);
    if (psram_cmd(PSRAM_CMD_EXIT_QPI, true) != UFI_OK) {
        return phase_failed(UFI_PSRAM_PH_QPI_RESET, true, NULL);
    }
    trial(UFI_PSRAM_PH_SPI_RESET);
    if (psram_cmd(PSRAM_CMD_RESET_EN, false) != UFI_OK || psram_cmd(PSRAM_CMD_RESET, false) != UFI_OK) {
        return phase_failed(UFI_PSRAM_PH_SPI_RESET, true, NULL);
    }
    HAL_Delay(1);

    trial(UFI_PSRAM_PH_ID);
    const int id_status = psram_read_id(diag.id);
    if (id_status != UFI_OK || diag.id[0] != PSRAM_ID_MF || diag.id[1] != PSRAM_ID_KGD) {
        /* The two bytes actually read (expected 0D 5D); "t/o" = the controller itself timed
         * out. Second opinion without the controller: the same ID by plain GPIO with a
         * pull-up on SO. 0D 5D there = chip fine, controller setup not; FF FF = the chip
         * does not drive SO at all. */
        psram_gpio_probe(diag.gpio_id);
        snprintf(status_text, sizeof(status_text), "PSRAM not answering (id %02X %02X%s) gpio %02X %02X",
                 diag.id[0], diag.id[1], id_status != UFI_OK ? " t/o" : "", diag.gpio_id[0], diag.gpio_id[1]);
        return phase_failed(UFI_PSRAM_PH_ID, id_status != UFI_OK, status_text);
    }

    /* the ID is read; everything from here on is allowed at the working clock */
    trial(UFI_PSRAM_PH_ENTER_QPI);
    const uint32_t busy_since = HAL_GetTick();
    while ((hospi.Instance->SR & OCTOSPI_SR_BUSY) != 0u) {
        if (HAL_GetTick() - busy_since > 10u) {
            return phase_failed(UFI_PSRAM_PH_ENTER_QPI, true, NULL);
        }
    }
    MODIFY_REG(hospi.Instance->DCR2, OCTOSPI_DCR2_PRESCALER, (uint32_t)(OSPI_PRESCALER - 1));
    hospi.Init.ClockPrescaler = OSPI_PRESCALER;
    if (psram_cmd(PSRAM_CMD_ENTER_QPI, false) != UFI_OK) {
        return phase_failed(UFI_PSRAM_PH_ENTER_QPI, true, NULL);
    }
    trial(UFI_PSRAM_PH_MAP);
    if (psram_memory_mapped() != UFI_OK) {
        return phase_failed(UFI_PSRAM_PH_MAP, true, NULL);
    }

    /* Memory test; the marker names the phase a dying start did not get past */
    volatile uint32_t* mem = (volatile uint32_t*)OCTOSPI1_BASE;
    psram_mpu(false);
    trial(UFI_PSRAM_PH_FIRST_READ);
    (void)mem[0];
    trial(UFI_PSRAM_PH_FIRST_WRITE);
    mem[0] = pat_s(0);
    trial(UFI_PSRAM_PH_NARROW);
    psram_narrow_test(mem, &diag.narrow);
    psram_narrow_test(mem + (1024u - 128u) / 4u, &diag.narrow);    /* across a 1 KB page boundary */
    trial(UFI_PSRAM_PH_LINES);
    psram_mpu(true);
    /* Bench 2026-11-12: a sequential CPU fill of 112 words left word 110 zero in the chip
     * although word 111 was right. With "dynamic read allocate" the Cortex-M7 stops loading
     * cache lines during a long sequential write and sends the stores straight to the bus;
     * a 32-bit store that is not merged with its neighbour then arrives as a 64-bit beat
     * with four byte strobes, and this OCTOSPI cannot mask bytes on a memory without DQS:
     * the other half of the doubleword is written as zero. With the mode off every write
     * goes through the cache and reaches the chip as whole lines only. Costs a line fill
     * before memset-like writes, for all memory. */
    SCnSCB->ACTLR |= SCnSCB_ACTLR_DISRAMODE_Msk;
    __DSB();
    __ISB();
    const char* lines = psram_line_test(mem);
    if (lines == NULL) {
        lines = psram_stream_test(mem);
    }
    if (lines != NULL) {
        diag.flags |= 2u;
    }
    psram_trial = 0;

    /* The store is handed out only when the window behaves like RAM for 32-bit writes
     * (DMA capture) and for cache lines (CPU). Otherwise the numbers go into INFO. */
    const narrow_t* nr = &diag.narrow;
    const bool exact = nr->fill == 0u && nr->asc == 0u && nr->desc == 0u && nr->lone == 0u && nr->tail == 0u;
    if (!exact) {
        snprintf(status_text, sizeof(status_text),
                 "PSRAM found, 32-bit writes unsafe (f%lu a%lu t%lu d%lu l%lu, lines %s)",
                 (unsigned long)nr->fill, (unsigned long)nr->asc, (unsigned long)nr->tail,
                 (unsigned long)nr->desc, (unsigned long)nr->lone, lines == NULL ? "ok" : "bad");
        return phase_failed(UFI_PSRAM_PH_NARROW, false, status_text);
    }
    if (lines != NULL) {
        return phase_failed(UFI_PSRAM_PH_LINES, false, lines);
    }
    diag.state = UFI_PSRAM_STATE_OK;
    diag.phase = UFI_PSRAM_PH_LINES;
    psram_result = "PSRAM 8 MB ok";
    return UFI_OK;
}

/* Result of the capture-stream write test (ufi_flux.c), which runs after this init */
void ufi_psram_dma_result(uint32_t result)
{
    diag.dma = result;
    if (result == 0u) {
        diag.phase = UFI_PSRAM_PH_DMA;
    } else if (result == UFI_DMA_TEST_NOT_RUN) {
        diag.state = UFI_PSRAM_STATE_DMA_NOT_RUN;
        psram_result = "PSRAM 8 MB ok, DMA test did not run";
    } else {
        diag.state = UFI_PSRAM_STATE_FAILED;
        diag.phase = UFI_PSRAM_PH_DMA;
        const uint32_t* d = ufi_flux_dma_detail();
        memcpy(diag.dma_detail, d, sizeof(diag.dma_detail));
        /* code 6 = the pattern fill by the CPU was already wrong (f = words), the stream
         * did not run; w[i]=v = index and value of the first wrong word */
        snprintf(status_text, sizeof(status_text),
                 "PSRAM found, DMA writes unsafe (run %lu code %lu: b%lu a%lu d%lu z%lu f%lu w[%lu]=%08lX)",
                 (unsigned long)(result >> 8), (unsigned long)(result & 0xFFu), (unsigned long)d[0],
                 (unsigned long)d[1], (unsigned long)d[2], (unsigned long)d[3], (unsigned long)d[6],
                 (unsigned long)d[5], (unsigned long)d[4]);
        psram_result = status_text;
    }
}

static uint16_t sat16(uint32_t v)
{
    return v > 0xFFFFu ? 0xFFFFu : (uint16_t)v;
}

/* 32 bytes, schema 1 (docs/USB_Protokoll.md, SELFTEST 'P') */
uint8_t ufi_psram_status_pack(uint8_t* out)
{
    const uint16_t n[4] = {sat16(diag.narrow.fill), sat16(diag.narrow.asc), sat16(diag.narrow.desc),
                           sat16(diag.narrow.lone)};
    const uint32_t rsr = ufi_crash_reset_flags();
    memset(out, 0, 32);
    out[0] = 1;
    out[1] = diag.state;
    out[2] = diag.phase;
    out[3] = diag.latched_phase;
    memcpy(&out[4], diag.id, 2);
    memcpy(&out[6], diag.gpio_id, 2);
    out[8] = diag.flags;
    out[9] = (uint8_t)diag.narrow.tail;
    memcpy(&out[10], n, 8);
    memcpy(&out[20], &diag.dma, 4);
    memcpy(&out[24], &rsr, 4);
    for (unsigned i = 0; i < 4u; i++) {     /* failed DMA test: guards before / after, data not increasing, data zero */
        out[28u + i] = diag.dma_detail[i] > 255u ? 255u : (uint8_t)diag.dma_detail[i];
    }
    return 32;
}

const char* ufi_psram_result(void)
{
    return psram_result;
}

void ufi_psram_note(const char* text)
{
    psram_result = text;
}

#else

void ufi_psram_skip(const char* why)
{
    (void)why;
}

void ufi_psram_note(const char* text)
{
    (void)text;
}

void ufi_psram_unlatch(void)
{
}

void ufi_psram_dma_result(uint32_t result)
{
    (void)result;
}

uint8_t ufi_psram_status_pack(uint8_t* out)
{
    memset(out, 0, 32);
    out[0] = 1;
    return 32;
}

int ufi_psram_init(void)
{
    return UFI_ERR_NOT_IMPL;
}

const char* ufi_psram_result(void)
{
    return "no PSRAM";
}

#endif
