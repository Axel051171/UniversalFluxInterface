/* Host-side stand-in for the STM32 HAL header: just enough declarations so that
 * HAL-bound production modules compile natively for fuzzing and analysis.  Every
 * function declared here is provided by a generated stub (see run_fuzz.py); nothing
 * in this file models hardware behaviour. */
#ifndef STM32H7XX_HAL_STUB_H
#define STM32H7XX_HAL_STUB_H
#include <stdint.h>
#include <stdbool.h>

/* Device identity reads must use host storage, never physical MCU addresses. */
extern uint32_t stub_uid[3];
#define UID_BASE ((uintptr_t)&stub_uid[0])
typedef struct { volatile uint32_t IDCODE; } DBGMCU_TypeDef;
extern DBGMCU_TypeDef stub_dbgmcu;
#define DBGMCU (&stub_dbgmcu)

typedef struct {
    volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2];
} GPIO_TypeDef;

typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;
typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2, HAL_TIMEOUT = 3 } HAL_StatusTypeDef;

extern GPIO_TypeDef stub_gpio[11];
#define GPIOA (&stub_gpio[0])
#define GPIOB (&stub_gpio[1])
#define GPIOC (&stub_gpio[2])
#define GPIOD (&stub_gpio[3])
#define GPIOE (&stub_gpio[4])
#define GPIOF (&stub_gpio[5])
#define GPIOG (&stub_gpio[6])
#define GPIOH (&stub_gpio[7])
#define GPIOI (&stub_gpio[8])
#define GPIOJ (&stub_gpio[9])
#define GPIOK (&stub_gpio[10])

#define GPIO_PIN_0  0x0001u
#define GPIO_PIN_1  0x0002u
#define GPIO_PIN_2  0x0004u
#define GPIO_PIN_3  0x0008u
#define GPIO_PIN_4  0x0010u
#define GPIO_PIN_5  0x0020u
#define GPIO_PIN_6  0x0040u
#define GPIO_PIN_7  0x0080u
#define GPIO_PIN_8  0x0100u
#define GPIO_PIN_9  0x0200u
#define GPIO_PIN_10 0x0400u
#define GPIO_PIN_11 0x0800u
#define GPIO_PIN_12 0x1000u
#define GPIO_PIN_13 0x2000u
#define GPIO_PIN_14 0x4000u
#define GPIO_PIN_15 0x8000u

#define GPIO_AF1_TIM2 1u
#define GPIO_AF2_TIM3 2u
#define TIM_CHANNEL_1 0u
#define TIM_CHANNEL_2 4u
#define TIM_CHANNEL_3 8u
#define TIM_CHANNEL_4 12u
#define TIM_INPUTCHANNELPOLARITY_RISING 0u

#define ADC_CHANNEL_0  0u
#define ADC_CHANNEL_1  1u
#define ADC_CHANNEL_2  2u
#define ADC_CHANNEL_3  3u
#define ADC_CHANNEL_4  4u
#define ADC_CHANNEL_5  5u
#define ADC_CHANNEL_6  6u
#define ADC_CHANNEL_7  7u
#define ADC_CHANNEL_8  8u
#define ADC_CHANNEL_9  9u
#define ADC_CHANNEL_10 10u
#define ADC_CHANNEL_11 11u
#define ADC_CHANNEL_12 12u
#define ADC_CHANNEL_13 13u
#define ADC_CHANNEL_14 14u
#define ADC_CHANNEL_15 15u
#define ADC_CHANNEL_16 16u
#define ADC_CHANNEL_17 17u
#define ADC_CHANNEL_18 18u
#define ADC_CHANNEL_19 19u

typedef struct { volatile uint32_t CTRL, CYCCNT; } DWT_Type;
extern DWT_Type stub_dwt;
#define DWT (&stub_dwt)
extern uint32_t SystemCoreClock;

typedef struct {
    volatile uint32_t CR1, CR2, SMCR, DIER, SR, EGR, CCMR1, CCMR2, CCER, CNT, PSC, ARR, RCR,
                      CCR1, CCR2, CCR3, CCR4, BDTR, DCR, DMAR;
} TIM_TypeDef;
extern TIM_TypeDef stub_tim[8];
#define TIM2 (&stub_tim[2])
#define TIM3 (&stub_tim[3])

void SCB_InvalidateDCache_by_Addr(void* addr, int32_t dsize);
void SCB_CleanDCache_by_Addr(void* addr, int32_t dsize);
void NVIC_SystemReset(void);
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint16_t pin);
void HAL_GPIO_TogglePin(GPIO_TypeDef* port, uint16_t pin);

extern uint32_t stub_primask;
static inline uint32_t __get_PRIMASK(void) {return stub_primask;}
static inline void __set_PRIMASK(uint32_t value) {stub_primask=value;}
static inline void __disable_irq(void) {stub_primask=1;}
static inline void __enable_irq(void) {stub_primask=0;}
static inline void __DSB(void) {}
static inline void __ISB(void) {}
static inline void __DMB(void) {}

#endif
