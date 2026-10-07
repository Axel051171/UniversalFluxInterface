/**
 * UFI Flux Engine - QSPI PSRAM (APS6404L, 8 MB) on OCTOSPI1
 *
 * Init in SPI mode (reset, read ID), switch the PSRAM to QPI, then map it at
 * OCTOSPI1_BASE (0x90000000) for reads and writes, so CPU and DMA use it like RAM.
 *   clock  : HCLK 275 MHz / 4 = 68.75 MHz (PSRAM: 133 MHz max)
 *   CSBOUND: 1 KB page (linear bursts must not cross a page)
 *   REFRESH: CS# released at least every 8 us (tCEM)
 */

#include "ufi_firmware.h"

#if BOARD_HAS_PSRAM

static OSPI_HandleTypeDef hospi;
static const char* psram_result = "PSRAM init failed";

#define PSRAM_CMD_RESET_EN      0x66
#define PSRAM_CMD_RESET         0x99
#define PSRAM_CMD_READ_ID       0x9F
#define PSRAM_CMD_ENTER_QPI     0x35
#define PSRAM_CMD_QUAD_READ     0xEB    /* QPI: 6 wait cycles */
#define PSRAM_CMD_QUAD_WRITE    0x38
#define PSRAM_QPI_READ_WAIT     6
#define PSRAM_ID_MF             0x0D    /* AP Memory */
#define PSRAM_ID_KGD            0x5D    /* known good die */

#define OSPI_PRESCALER          4
#define OSPI_CLK_HZ             (275000000UL / OSPI_PRESCALER)
#define PSRAM_TCEM_CYCLES       ((OSPI_CLK_HZ / 1000000UL) * 8UL)   /* 8 us */

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
    return HAL_OSPI_Command(&hospi, &c, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) == HAL_OK ? UFI_OK : UFI_ERR_TIMEOUT;
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
    if (HAL_OSPI_Command(&hospi, &c, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK ||
        HAL_OSPI_Receive(&hospi, id, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
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
    if (HAL_OSPI_Command(&hospi, &c, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return UFI_ERR_TIMEOUT;
    }
    c.OperationType = HAL_OSPI_OPTYPE_WRITE_CFG;
    c.Instruction = PSRAM_CMD_QUAD_WRITE;
    c.DummyCycles = 0;
    if (HAL_OSPI_Command(&hospi, &c, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return UFI_ERR_TIMEOUT;
    }

    OSPI_MemoryMappedTypeDef mm = {0};
    mm.TimeOutActivation = HAL_OSPI_TIMEOUT_COUNTER_DISABLE;
    return HAL_OSPI_MemoryMapped(&hospi, &mm) == HAL_OK ? UFI_OK : UFI_ERR_TIMEOUT;
}

/* MPU: normal, cacheable memory for the mapped window, no speculative access beyond 8 MB */
static void psram_mpu(void)
{
    HAL_MPU_Disable();
    MPU_Region_InitTypeDef r = {0};
    r.Enable = MPU_REGION_ENABLE;
    r.Number = MPU_REGION_NUMBER0;
    r.BaseAddress = OCTOSPI1_BASE;
    r.Size = MPU_REGION_SIZE_8MB;
    r.AccessPermission = MPU_REGION_FULL_ACCESS;
    r.TypeExtField = MPU_TEX_LEVEL1;            /* normal memory, write-back */
    r.IsCacheable = MPU_ACCESS_CACHEABLE;
    r.IsBufferable = MPU_ACCESS_BUFFERABLE;
    r.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.SubRegionDisable = 0;
    HAL_MPU_ConfigRegion(&r);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/* Returns UFI_OK when the PSRAM answered with the right ID and passed a pattern test */
int ufi_psram_init(void)
{
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
    hospi.Init.ClockPrescaler = OSPI_PRESCALER;
    hospi.Init.SampleShifting = HAL_OSPI_SAMPLE_SHIFTING_NONE;
    hospi.Init.DelayHoldQuarterCycle = HAL_OSPI_DHQC_DISABLE;
    hospi.Init.ChipSelectBoundary = 10;                 /* 1 KB page */
    hospi.Init.DelayBlockBypass = HAL_OSPI_DELAY_BLOCK_BYPASSED;
    hospi.Init.MaxTran = 0;
    hospi.Init.Refresh = PSRAM_TCEM_CYCLES - 16;
    if (HAL_OSPI_Init(&hospi) != HAL_OK) {
        return UFI_ERR_TIMEOUT;
    }

    OSPIM_CfgTypeDef m = {0};
    m.ClkPort = 1;
    m.NCSPort = 1;
    m.IOLowPort = HAL_OSPIM_IOPORT_1_LOW;
    m.IOHighPort = HAL_OSPIM_IOPORT_NONE;
    m.Req2AckTime = 1;
    if (HAL_OSPIM_Config(&hospi, &m, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return UFI_ERR_TIMEOUT;
    }

    HAL_Delay(1);                                       /* tPU 150 us after power-up */
    psram_cmd(PSRAM_CMD_RESET_EN, false);
    psram_cmd(PSRAM_CMD_RESET, false);
    HAL_Delay(1);

    uint8_t id[2] = {0, 0};
    if (psram_read_id(id) != UFI_OK || id[0] != PSRAM_ID_MF || id[1] != PSRAM_ID_KGD) {
        psram_result = "PSRAM not answering";
        return UFI_ERR_NOT_IMPL;                        /* not fitted / not answering */
    }
    if (psram_cmd(PSRAM_CMD_ENTER_QPI, false) != UFI_OK || psram_memory_mapped() != UFI_OK) {
        return UFI_ERR_TIMEOUT;
    }
    psram_mpu();

    /* Self-test: data lines (walking one), address lines (unique value at every power-of-
     * two word offset, catches shorted/open address bits), pattern in each MB */
    volatile uint32_t* mem = (volatile uint32_t*)OCTOSPI1_BASE;
    const uint32_t words = PSRAM_SIZE_BYTES / 4u;
    for (uint32_t bit = 1; bit != 0; bit <<= 1) {
        mem[0] = bit;
        SCB_CleanInvalidateDCache();
        if (mem[0] != bit) {
            psram_result = "PSRAM data line fault";
            return UFI_ERR_DMA;
        }
    }
    mem[0] = 0x5555AAAAu;
    for (uint32_t a = 1; a < words; a <<= 1) {
        mem[a] = 0xC0DE0000u ^ a;
    }
    SCB_CleanInvalidateDCache();
    if (mem[0] != 0x5555AAAAu) {
        psram_result = "PSRAM address line fault";
        return UFI_ERR_DMA;
    }
    for (uint32_t a = 1; a < words; a <<= 1) {
        if (mem[a] != (0xC0DE0000u ^ a)) {
            psram_result = "PSRAM address line fault";
            return UFI_ERR_DMA;
        }
    }
    for (uint32_t a = 0; a < words; a += words / 8u) {
        mem[a + 3] = 0xA5A50000u ^ a;
        mem[a + 4] = ~(0xA5A50000u ^ a);
    }
    SCB_CleanInvalidateDCache();
    for (uint32_t a = 0; a < words; a += words / 8u) {
        if (mem[a + 3] != (0xA5A50000u ^ a) || mem[a + 4] != ~(0xA5A50000u ^ a)) {
            psram_result = "PSRAM pattern fault";
            return UFI_ERR_DMA;
        }
    }
    psram_result = "PSRAM 8 MB ok";
    return UFI_OK;
}

const char* ufi_psram_result(void)
{
    return psram_result;
}

#else

int ufi_psram_init(void)
{
    return UFI_ERR_NOT_IMPL;
}

const char* ufi_psram_result(void)
{
    return "no PSRAM";
}

#endif
