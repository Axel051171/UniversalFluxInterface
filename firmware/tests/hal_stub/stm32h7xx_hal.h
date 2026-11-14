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

/* ---- declarations for the write path (ufi_write.c): register bits with the STM32H7 values,
 * HAL types reduced to the members the firmware uses.  The registers are plain host memory:
 * a write has no side effect (no rc_w0 flags, no update generation, no DMA). ---- */
#define TIM_CR1_CEN   0x0001u
#define TIM_CR1_DIR   0x0010u
#define TIM_CR1_ARPE  0x0080u
#define TIM_DIER_UIE   0x0001u
#define TIM_DIER_CC2IE 0x0004u
#define TIM_DIER_CC4IE 0x0010u
#define TIM_DIER_UDE   0x0100u
#define TIM_SR_UIF    0x0001u
#define TIM_SR_CC2IF  0x0004u
#define TIM_SR_CC4IF  0x0010u
#define TIM_SR_CC2OF  0x0400u
/* A harness that has to see the update generation (the shadow registers load from ARR and
 * CCR1 at that moment, and plain memory keeps no trace of it) compiles with
 * -DUFI_STUB_TIM_UG_HOOK=<function>: the function is called where the firmware writes
 * TIM_EGR_UG and returns the bit value, which the firmware then stores in EGR. */
#ifdef UFI_STUB_TIM_UG_HOOK
uint32_t UFI_STUB_TIM_UG_HOOK(void);
#define TIM_EGR_UG    UFI_STUB_TIM_UG_HOOK()
#else
#define TIM_EGR_UG    0x0001u
#endif
#define TIM_CCMR1_CC2S  0x00000300u
#define TIM_CCMR1_OC1PE 0x00000008u
#define TIM_CCMR1_OC1M  0x00010070u
#define TIM_CCMR1_OC2M  0x01007000u
#define TIM_CCER_CC1P 0x0002u
#define TIM_CCER_CC2E 0x0010u
#define TIM_OCMODE_TOGGLE          0x0030u
#define TIM_OCMODE_FORCED_INACTIVE 0x0040u
#define TIM_OCMODE_PWM1            0x0060u
#define TIM_OCMODE_PWM2            0x0070u
#define TIM_COUNTERMODE_DOWN           TIM_CR1_DIR
#define TIM_CLOCKDIVISION_DIV1         0u
#define TIM_AUTORELOAD_PRELOAD_ENABLE  TIM_CR1_ARPE
#define TIM_OCPOLARITY_HIGH 0u
#define TIM_OCPOLARITY_LOW  TIM_CCER_CC1P
#define TIM_OCFAST_DISABLE  0u
#define TIM_CCx_ENABLE      1u
#define TIM_DMABASE_ARR                0x000Bu
#define TIM_DMABURSTLENGTH_3TRANSFERS  0x0200u

typedef struct {
    uint32_t Prescaler, CounterMode, Period, ClockDivision, RepetitionCounter, AutoReloadPreload;
} TIM_Base_InitTypeDef;
typedef struct { TIM_TypeDef* Instance; TIM_Base_InitTypeDef Init; } TIM_HandleTypeDef;
typedef struct {
    uint32_t OCMode, Pulse, OCPolarity, OCNPolarity, OCFastMode, OCIdleState, OCNIdleState;
} TIM_OC_InitTypeDef;
#define __HAL_TIM_ENABLE(h)  ((h)->Instance->CR1 |= TIM_CR1_CEN)
#define __HAL_TIM_DISABLE(h) ((h)->Instance->CR1 &= ~TIM_CR1_CEN)

typedef struct { volatile uint32_t CR, NDTR, PAR, M0AR, M1AR, FCR; } DMA_Stream_TypeDef;
typedef struct { volatile uint32_t LISR, HISR, LIFCR, HIFCR; } DMA_TypeDef;
extern DMA_TypeDef stub_dma[2];
extern DMA_Stream_TypeDef stub_dma_stream[16];
#define DMA1         (&stub_dma[0])
#define DMA1_Stream1 (&stub_dma_stream[1])
#define DMA_SxCR_EN   0x0001u
#define DMA_SxCR_TEIE 0x0004u
#define DMA_SxCR_HTIE 0x0008u
#define DMA_SxCR_TCIE 0x0010u
#define DMA_LISR_FEIF1   0x0040u
#define DMA_LISR_DMEIF1  0x0100u
#define DMA_LISR_TEIF1   0x0200u
#define DMA_LISR_HTIF1   0x0400u
#define DMA_LISR_TCIF1   0x0800u
#define DMA_LIFCR_CFEIF1  0x0040u
#define DMA_LIFCR_CDMEIF1 0x0100u
#define DMA_LIFCR_CTEIF1  0x0200u
#define DMA_LIFCR_CHTIF1  0x0400u
#define DMA_LIFCR_CTCIF1  0x0800u
#define DMA_REQUEST_TIM3_UP       27u
#define DMA_MEMORY_TO_PERIPH      0x00000040u
#define DMA_PINC_DISABLE          0u
#define DMA_MINC_ENABLE           0x00000400u
#define DMA_PDATAALIGN_HALFWORD   0x00000800u
#define DMA_MDATAALIGN_HALFWORD   0x00002000u
#define DMA_CIRCULAR              0x00000100u
#define DMA_PRIORITY_VERY_HIGH    0x00030000u
#define DMA_FIFOMODE_DISABLE      0u
typedef struct {
    uint32_t Request, Direction, PeriphInc, MemInc, PeriphDataAlignment, MemDataAlignment, Mode,
             Priority, FIFOMode, FIFOThreshold, MemBurst, PeriphBurst;
} DMA_InitTypeDef;
typedef enum { HAL_UNLOCKED = 0, HAL_LOCKED = 1 } HAL_LockTypeDef;
typedef enum { HAL_DMA_STATE_RESET = 0, HAL_DMA_STATE_READY = 1, HAL_DMA_STATE_BUSY = 2 } HAL_DMA_StateTypeDef;
typedef struct {
    DMA_Stream_TypeDef* Instance; DMA_InitTypeDef Init; HAL_LockTypeDef Lock; volatile HAL_DMA_StateTypeDef State;
} DMA_HandleTypeDef;
#define __HAL_UNLOCK(h) do { (h)->Lock = HAL_UNLOCKED; } while (0)

typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
#define GPIO_MODE_INPUT     0u
#define GPIO_MODE_OUTPUT_PP 1u
#define GPIO_MODE_AF_PP     2u
#define GPIO_NOPULL         0u
#define GPIO_SPEED_FREQ_LOW       0u
#define GPIO_SPEED_FREQ_VERY_HIGH 3u

typedef enum { DMA1_Stream1_IRQn = 12, TIM3_IRQn = 29, OTG_HS_IRQn = 77 } IRQn_Type;
/* USB device controller, as far as ufi_usb.c names it (polled mass storage modes) */
typedef struct { int unused; } PCD_HandleTypeDef;
void HAL_PCD_IRQHandler(PCD_HandleTypeDef* hpcd);
void HAL_NVIC_DisableIRQ(IRQn_Type irq);
void NVIC_ClearPendingIRQ(IRQn_Type irq);
#define __ALIGN_BEGIN
#define __ALIGN_END
/* peripheral clocks: there is no RCC on the host */
#define __HAL_RCC_TIM3_CLK_ENABLE()    do { } while (0)
#define __HAL_RCC_DMA1_CLK_ENABLE()    do { } while (0)
#define __HAL_RCC_D2SRAM2_CLK_ENABLE() do { } while (0)

HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef* htim);
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef* htim, const TIM_OC_InitTypeDef* config, uint32_t channel);
void TIM_CCxChannelCmd(TIM_TypeDef* tim, uint32_t channel, uint32_t state);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef* hdma);
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef* hdma, uint32_t source, uint32_t destination, uint32_t length);
void HAL_GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* init);
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t preempt, uint32_t sub);
void HAL_NVIC_EnableIRQ(IRQn_Type irq);

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
#ifndef UFI_STUB_OWN_IPSR
static inline uint32_t __get_IPSR(void) {return 0;}     /* thread mode; ufi_composite_runtime.c brings its own */
#endif
static inline void __set_PRIMASK(uint32_t value) {stub_primask=value;}
static inline void __disable_irq(void) {stub_primask=1;}
static inline void __enable_irq(void) {stub_primask=0;}
static inline void __DSB(void) {}
static inline void __ISB(void) {}
static inline void __DMB(void) {}

#endif
