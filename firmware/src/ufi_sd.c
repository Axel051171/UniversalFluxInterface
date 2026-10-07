/**
 * UFI Flux Engine - microSD slot (board v0.5, J10 Molex 104031-0811)
 *
 * SDMMC2, 4 bit: CK PD6, CMD PD7, D0 PG9, D1 PG10 (AF11), D2 PG11, D3 PG12 (AF10);
 * 47k pull-ups on the board.  Card detect PG13: switch to GND, low = card inserted
 * (to be confirmed on the first board; the MCU pull-up keeps it high without a card).
 * Kernel clock: PLL1Q 100 MHz -> 25 MHz bus clock (ClockDiv 2).
 *
 * So far the card is only detected and initialised (SD_INFO); a file system for
 * stand-alone imaging would build on this.
 */

#include "ufi_firmware.h"

static SD_HandleTypeDef hsd;
static bool sd_inited;

bool ufi_sd_present(void)
{
    return HAL_GPIO_ReadPin(PIN_SD_CD.port, PIN_SD_CD.pin) == GPIO_PIN_RESET;
}

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

int ufi_sd_info(sd_info_t* info)
{
    *info = (sd_info_t){0};
    info->present = ufi_sd_present();
    if (!info->present) {
        if (sd_inited) {
            HAL_SD_DeInit(&hsd);
            sd_inited = false;
        }
        info->status = (uint8_t)(-UFI_ERR_NOT_IMPL);
        return UFI_OK;
    }
    if (sd_inited) {
        HAL_SD_DeInit(&hsd);                           /* card may have been swapped */
        sd_inited = false;
    }

    hsd.Instance = SDMMC2;
    hsd.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hsd.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd.Init.BusWide = SDMMC_BUS_WIDE_4B;
    hsd.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
    hsd.Init.ClockDiv = 2;                             /* 100 MHz / (2 * 2) = 25 MHz */
    if (HAL_SD_Init(&hsd) != HAL_OK) {
        info->status = (uint8_t)(-UFI_ERR_TIMEOUT);
        return UFI_OK;
    }
    sd_inited = true;
    info->bus_width = (HAL_SD_ConfigWideBusOperation(&hsd, SDMMC_BUS_WIDE_4B) == HAL_OK) ? 4 : 1;

    HAL_SD_CardInfoTypeDef ci;
    if (HAL_SD_GetCardInfo(&hsd, &ci) != HAL_OK) {
        info->status = (uint8_t)(-UFI_ERR_TIMEOUT);
        return UFI_OK;
    }
    info->card_type = (uint8_t)ci.CardType;
    info->capacity_mb = (uint32_t)(((uint64_t)ci.BlockNbr * ci.BlockSize) >> 20);
    info->status = 0;
    return UFI_OK;
}
