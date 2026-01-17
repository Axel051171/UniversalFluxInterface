/**
 * MIG Dumper - STM32G0B1 Firmware
 * Universal EPROM/Mask ROM Dumper
 * Unterstützt: 2716 bis 27C322 (2KB - 4MB)
 */

#include "stm32g0xx_hal.h"
#include <string.h>
#include <stdbool.h>

/* ============================================================================
 * PIN DEFINITIONEN
 * ============================================================================ */

#define ADDR_PORT_L     GPIOA   // A0-A7 auf PA0-PA7
#define ADDR_PORT_H     GPIOB   // A8-A15 auf PB0-PB7
#define ADDR_PORT_X     GPIOC   // A16-A19 auf PC0-PC3

#define DATA_PORT_L     GPIOA   // D0-D7 auf PA8-PA15
#define DATA_PORT_H     GPIOB   // D8-D15 auf PB8-PB15

#define PIN_CE          GPIO_PIN_4
#define PIN_OE          GPIO_PIN_5
#define PIN_VPP_EN      GPIO_PIN_6
#define CTRL_PORT       GPIOC

#define PIN_LED         GPIO_PIN_13
#define LED_PORT        GPIOC

/* ============================================================================
 * ROM TYPEN
 * ============================================================================ */

typedef struct {
    const char* name;
    uint32_t size;
    uint8_t pins;
    uint8_t addr_bits;
    uint8_t data_bits;
    uint16_t access_ns;
} rom_type_t;

static const rom_type_t ROM_TYPES[] = {
    {"2716",    2048,       24, 11, 8,  450},
    {"2732",    4096,       24, 12, 8,  450},
    {"2764",    8192,       28, 13, 8,  250},
    {"27128",   16384,      28, 14, 8,  250},
    {"27256",   32768,      28, 15, 8,  200},
    {"27512",   65536,      28, 16, 8,  200},
    {"27C010",  131072,     32, 17, 8,  150},
    {"27C020",  262144,     32, 18, 8,  150},
    {"27C040",  524288,     32, 19, 8,  120},
    {"27C080",  1048576,    32, 20, 8,  120},
    {"27C160",  2097152,    42, 20, 16, 100},
    {"27C322",  4194304,    42, 21, 16, 100},
    {NULL, 0, 0, 0, 0, 0}
};

#define ROM_TYPE_COUNT 12

static uint8_t g_rom_type = 0xFF;
static uint8_t g_data_width = 8;
static uint32_t g_rom_size = 0;

/* ============================================================================
 * BUS FUNKTIONEN
 * ============================================================================ */

static inline void set_address(uint32_t addr) {
    ADDR_PORT_L->ODR = (ADDR_PORT_L->ODR & 0xFF00) | (addr & 0xFF);
    ADDR_PORT_H->ODR = (ADDR_PORT_H->ODR & 0xFF00) | ((addr >> 8) & 0xFF);
    ADDR_PORT_X->ODR = (ADDR_PORT_X->ODR & 0xFFF0) | ((addr >> 16) & 0x0F);
}

static inline uint8_t read_data_8(void) {
    return (DATA_PORT_L->IDR >> 8) & 0xFF;
}

static inline uint16_t read_data_16(void) {
    return ((DATA_PORT_H->IDR >> 8) << 8) | ((DATA_PORT_L->IDR >> 8) & 0xFF);
}

static inline void delay_ns(uint32_t ns) {
    for (volatile uint32_t i = 0; i < ns/16; i++) __NOP();
}

/* ============================================================================
 * ROM LESEN
 * ============================================================================ */

uint8_t rom_read_byte(uint32_t addr) {
    set_address(addr);
    CTRL_PORT->BSRR = (PIN_CE | PIN_OE) << 16;  // CE=0, OE=0
    delay_ns(200);
    uint8_t data = read_data_8();
    CTRL_PORT->BSRR = PIN_CE | PIN_OE;          // CE=1, OE=1
    return data;
}

uint16_t rom_read_word(uint32_t addr) {
    set_address(addr);
    CTRL_PORT->BSRR = (PIN_CE | PIN_OE) << 16;
    delay_ns(200);
    uint16_t data = read_data_16();
    CTRL_PORT->BSRR = PIN_CE | PIN_OE;
    return data;
}

void rom_read_block(uint32_t addr, uint8_t* buf, uint32_t len) {
    if (g_data_width == 16) {
        for (uint32_t i = 0; i < len; i += 2) {
            uint16_t w = rom_read_word(addr++);
            buf[i] = w & 0xFF;
            buf[i+1] = (w >> 8) & 0xFF;
        }
    } else {
        for (uint32_t i = 0; i < len; i++) {
            buf[i] = rom_read_byte(addr++);
        }
    }
}

/* ============================================================================
 * ROM ERKENNUNG
 * ============================================================================ */

int detect_rom(void) {
    uint8_t b0 = rom_read_byte(0);
    uint8_t b1 = rom_read_byte(1);
    
    if (b0 == 0xFF && b1 == 0xFF) return -1;  // Kein ROM
    
    for (int i = ROM_TYPE_COUNT - 1; i >= 0; i--) {
        uint32_t test_addr = ROM_TYPES[i].size / 2;
        if (rom_read_byte(test_addr) != b0 || rom_read_byte(test_addr+1) != b1) {
            g_rom_type = i;
            g_data_width = ROM_TYPES[i].data_bits;
            g_rom_size = ROM_TYPES[i].size;
            return i;
        }
    }
    
    g_rom_type = 0;
    g_data_width = 8;
    g_rom_size = ROM_TYPES[0].size;
    return 0;
}

/* ============================================================================
 * CRC32
 * ============================================================================ */

static uint32_t crc32_table[256];

void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320 : 0);
        crc32_table[i] = c;
    }
}

uint32_t crc32_calc(const uint8_t* data, uint32_t len, uint32_t crc) {
    crc = ~crc;
    for (uint32_t i = 0; i < len; i++) crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/* ============================================================================
 * GPIO INIT
 * ============================================================================ */

void GPIO_Init(void) {
    GPIO_InitTypeDef gpio = {0};
    
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    
    // Address Output
    gpio.Pin = 0x00FF;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_GPIO_Init(GPIOB, &gpio);
    
    gpio.Pin = 0x000F;
    HAL_GPIO_Init(GPIOC, &gpio);
    
    // Data Input
    gpio.Pin = 0xFF00;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_GPIO_Init(GPIOB, &gpio);
    
    // Control
    gpio.Pin = PIN_CE | PIN_OE | PIN_VPP_EN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(CTRL_PORT, &gpio);
    HAL_GPIO_WritePin(CTRL_PORT, PIN_CE | PIN_OE, GPIO_PIN_SET);
    HAL_GPIO_WritePin(CTRL_PORT, PIN_VPP_EN, GPIO_PIN_RESET);
    
    // LED
    gpio.Pin = PIN_LED;
    HAL_GPIO_Init(LED_PORT, &gpio);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    HAL_Init();
    GPIO_Init();
    crc32_init();
    
    // Startup Blink
    for (int i = 0; i < 6; i++) {
        HAL_GPIO_TogglePin(LED_PORT, PIN_LED);
        HAL_Delay(100);
    }
    
    while (1) {
        HAL_GPIO_TogglePin(LED_PORT, PIN_LED);
        HAL_Delay(500);
    }
}
