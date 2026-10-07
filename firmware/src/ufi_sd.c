/**
 * UFI Flux Engine - SD NAND storage (board v0.6, U15 CSNP32GCR01-AOW / MKDV8GIL-AST)
 *
 * SDMMC2, 4 bit: CK PD6, CMD PD7, D0 PG9, D1 PG10 (AF11), D2 PG11, D3 PG12 (AF10);
 * 47k pull-ups on the board.  The chip is soldered: no card detect, it is initialised
 * once at start-up.  Kernel clock: PLL1Q 100 MHz -> 25 MHz bus clock (ClockDiv 2).
 *
 * Block access is CPU polled (hardware flow control stops the bus clock when the FIFO
 * is full/empty), so buffers may live in any RAM, DTCM included, and need no cache care.
 * Users: FatFs (disk_* glue below, stand-alone dumps) and the USB mass storage mode;
 * never both at once (ufi_msc.c unmounts the file system first).
 */

#include "ufi_firmware.h"
#include "ff.h"
#include "diskio.h"

#define SD_TIMEOUT_MS   1000u

static SD_HandleTypeDef hsd;
static bool sd_ready;
static int sd_init_result = UFI_ERR_NOT_IMPL;   /* until ufi_sd_init() ran */
static uint32_t sd_blocks;
static uint8_t sd_bus_width;

void HAL_SD_MspInit(SD_HandleTypeDef* h)
{
    (void)h;
    RCC_PeriphCLKInitTypeDef p = {0};
    p.PeriphClockSelection = RCC_PERIPHCLK_SDMMC;
    p.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL;    /* PLL1Q = 100 MHz */
    HAL_RCCEx_PeriphCLKConfig(&p);
    __HAL_RCC_SDMMC2_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;                              /* 47k on the board */
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7;                   /* CK, CMD */
    g.Alternate = GPIO_AF11_SDMMC2;
    HAL_GPIO_Init(GPIOD, &g);
    g.Pin = GPIO_PIN_9 | GPIO_PIN_10;                  /* D0, D1 */
    HAL_GPIO_Init(GPIOG, &g);
    g.Pin = GPIO_PIN_11 | GPIO_PIN_12;                 /* D2, D3 */
    g.Alternate = GPIO_AF10_SDMMC2;
    HAL_GPIO_Init(GPIOG, &g);
}

void HAL_SD_MspDeInit(SD_HandleTypeDef* h)
{
    (void)h;
    __HAL_RCC_SDMMC2_CLK_DISABLE();
}

int ufi_sd_init(void)
{
    if (sd_ready) {
        return UFI_OK;
    }
    hsd.Instance = SDMMC2;
    hsd.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hsd.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd.Init.BusWide = SDMMC_BUS_WIDE_4B;
    hsd.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
    hsd.Init.ClockDiv = 2;                             /* 100 MHz / (2 * 2) = 25 MHz */
    if (HAL_SD_Init(&hsd) != HAL_OK) {
        sd_init_result = UFI_ERR_TIMEOUT;
        return sd_init_result;
    }
    sd_bus_width = (HAL_SD_ConfigWideBusOperation(&hsd, SDMMC_BUS_WIDE_4B) == HAL_OK) ? 4 : 1;
    HAL_SD_CardInfoTypeDef ci;
    if (HAL_SD_GetCardInfo(&hsd, &ci) != HAL_OK || ci.BlockSize != 512u) {
        sd_init_result = UFI_ERR_TIMEOUT;
        return sd_init_result;
    }
    sd_blocks = ci.BlockNbr;
    sd_ready = true;
    sd_init_result = UFI_OK;
    return UFI_OK;
}

bool ufi_sd_present(void)
{
    return sd_ready;
}

uint32_t ufi_sd_blocks(void)
{
    return sd_ready ? sd_blocks : 0u;
}

/* after a transfer the chip programs/reads internally: wait for TRANSFER state */
static int wait_transfer(void)
{
    const uint32_t t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() - t0 > SD_TIMEOUT_MS) {
            return UFI_ERR_TIMEOUT;
        }
    }
    return UFI_OK;
}

int ufi_sd_read(uint8_t* buf, uint32_t lba, uint32_t count)
{
    if (!sd_ready) {
        return UFI_ERR_NOT_IMPL;
    }
    if (HAL_SD_ReadBlocks(&hsd, buf, lba, count, SD_TIMEOUT_MS) != HAL_OK) {
        return UFI_ERR_TIMEOUT;
    }
    return wait_transfer();
}

int ufi_sd_write(const uint8_t* buf, uint32_t lba, uint32_t count)
{
    if (!sd_ready) {
        return UFI_ERR_NOT_IMPL;
    }
    if (HAL_SD_WriteBlocks(&hsd, (uint8_t*)buf, lba, count, SD_TIMEOUT_MS) != HAL_OK) {
        return UFI_ERR_TIMEOUT;
    }
    return wait_transfer();
}

int ufi_sd_info(sd_info_t* info)
{
    *info = (sd_info_t){0};
    ufi_sd_init();                                     /* no-op once it worked */
    info->present = sd_ready;
    info->status = (uint8_t)(sd_init_result < 0 ? -sd_init_result : 0);
    if (sd_ready) {
        info->card_type = (uint8_t)hsd.SdCard.CardType;
        info->bus_width = sd_bus_width;
        info->capacity_mb = sd_blocks >> 11;           /* 2048 blocks of 512 bytes */
    }
    return UFI_OK;
}

/* ============================================================================
 * FatFs disk glue (one volume, drive 0)
 * ============================================================================ */

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv == 0 && sd_ready) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    return (pdrv == 0 && ufi_sd_init() == UFI_OK) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count)
{
    if (pdrv != 0) {
        return RES_PARERR;
    }
    return ufi_sd_read(buff, sector, count) == UFI_OK ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count)
{
    if (pdrv != 0) {
        return RES_PARERR;
    }
    return ufi_sd_write(buff, sector, count) == UFI_OK ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff)
{
    if (pdrv != 0 || !sd_ready) {
        return RES_NOTRDY;
    }
    switch (cmd) {
        case CTRL_SYNC:                                /* every write waits for TRANSFER */
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(DWORD*)buff = sd_blocks;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD*)buff = 512;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 1;                         /* erase block unknown */
            return RES_OK;
        default:
            return RES_PARERR;
    }
}
