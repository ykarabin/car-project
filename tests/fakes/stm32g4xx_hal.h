#ifndef TEST_STM32G4XX_HAL_H
#define TEST_STM32G4XX_HAL_H

#include <stdint.h>

typedef struct {
    uint32_t ODR;
    uint32_t IDR;
    uint32_t BSRR;
    uint32_t MODER;
    uint32_t OTYPER;
} GPIO_TypeDef;

typedef struct {
    uint32_t CR1;
    uint32_t ARR;
    uint32_t compare[3];
    uint32_t active_compare[3];
    uint32_t SR;
    uint32_t capture[2];
    uint32_t CNT;
    uint32_t BDTR;
    uint32_t CR2;
} TIM_TypeDef;

typedef struct {
    volatile uint32_t AHB2ENR, APB2ENR, CSR;
} RCC_TypeDef;
typedef struct {
    volatile uint32_t APB1FZR1, APB2FZ;
} DBGMCU_TypeDef;
typedef struct {
    volatile uint32_t PR1;
} EXTI_TypeDef;
typedef struct {
    volatile uint32_t KR, PR, RLR, SR, WINR;
} IWDG_TypeDef;

typedef struct {
    TIM_TypeDef *Instance;
} TIM_HandleTypeDef;

typedef struct {
    uint32_t unused;
} DMA_HandleTypeDef;
typedef struct {
    uint32_t ISR;
} USART_TypeDef;
typedef struct {
    USART_TypeDef *Instance;
    uint32_t RxState;
} UART_HandleTypeDef;
#define HAL_UART_STATE_READY UINT32_C(0)
#define HAL_UART_STATE_BUSY_RX UINT32_C(1)
#define USART_ISR_PE UINT32_C(1)
#define USART_ISR_FE UINT32_C(2)
#define USART_ISR_NE UINT32_C(4)
#define USART_ISR_ORE UINT32_C(8)
#define USART_ISR_RTOF UINT32_C(0x800)

typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;
typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2 } HAL_StatusTypeDef;

extern GPIO_TypeDef test_gpiob;
extern GPIO_TypeDef test_gpioc;
extern GPIO_TypeDef test_gpiod;
extern GPIO_TypeDef test_gpiof;
extern TIM_TypeDef test_tim1;
extern TIM_TypeDef test_tim4;
extern TIM_TypeDef test_tim2;
extern TIM_TypeDef test_tim3;
extern TIM_TypeDef test_tim6;
extern RCC_TypeDef test_rcc;
extern DBGMCU_TypeDef test_dbgmcu;
extern EXTI_TypeDef test_exti;
extern IWDG_TypeDef test_iwdg;

#define GPIOB (&test_gpiob)
#define GPIOC (&test_gpioc)
#define GPIOD (&test_gpiod)
#define GPIOF (&test_gpiof)
#define TIM1 (&test_tim1)
#define TIM4 (&test_tim4)
#define TIM2 (&test_tim2)
#define TIM3 (&test_tim3)
#define TIM6 (&test_tim6)
#define RCC (&test_rcc)
#define DBGMCU (&test_dbgmcu)
#define EXTI (&test_exti)
#define IWDG (&test_iwdg)
#define RCC_AHB2ENR_GPIODEN UINT32_C(0x00000008)
#define RCC_APB2ENR_TIM1EN UINT32_C(0x00000800)
#define RCC_CSR_RMVF UINT32_C(0x00800000)
#define RCC_CSR_IWDGRSTF UINT32_C(0x20000000)
#define DBGMCU_APB1FZR1_DBG_IWDG_STOP UINT32_C(0x00001000)
#define DBGMCU_APB2FZ_DBG_TIM1_STOP UINT32_C(0x00000800)
#define IWDG_SR_PVU UINT32_C(1)
#define IWDG_SR_RVU UINT32_C(2)
#define IWDG_SR_WVU UINT32_C(4)
#define TIM_SR_UIF UINT32_C(1)
#define TIM_CR1_UDIS UINT32_C(2)
#define TIM_BDTR_MOE UINT32_C(0x8000)
#define TIM_BDTR_OSSI UINT32_C(0x0400)
#define TIM_CR2_OIS1 UINT32_C(0x0100)
#define TIM_CR2_OIS2 UINT32_C(0x0400)
#define TIM_CR2_OIS3 UINT32_C(0x1000)
#define GPIO_PIN_7 UINT16_C(0x0080)
#define GPIO_PIN_2 UINT16_C(0x0004)
#define GPIO_PIN_13 UINT16_C(0x2000)
#define GPIO_PIN_8 UINT16_C(0x0100)
#define GPIO_PIN_9 UINT16_C(0x0200)
#define GPIO_PIN_10 UINT16_C(0x0400)
#define GPIO_PIN_11 UINT16_C(0x0800)
#define GPIO_PIN_12 UINT16_C(0x1000)
#define GPIO_PIN_14 UINT16_C(0x4000)
#define GPIO_PIN_15 UINT16_C(0x8000)
#define TIM_CHANNEL_1 UINT32_C(0)
#define TIM_CHANNEL_2 UINT32_C(4)
#define TIM_CHANNEL_3 UINT32_C(8)
#define TIM_CHANNEL_ALL UINT32_C(0x3C)
#define TIM_EVENTSOURCE_UPDATE UINT32_C(1)
#define TIM_FLAG_CC1 UINT32_C(0x0002)
#define TIM_FLAG_CC2 UINT32_C(0x0004)
#define TIM_FLAG_CC1OF UINT32_C(0x0200)
#define TIM_FLAG_CC2OF UINT32_C(0x0400)

#define __HAL_TIM_GET_AUTORELOAD(timer) ((timer)->Instance->ARR)
#define __HAL_TIM_GET_COUNTER(timer) ((timer)->Instance->CNT)
#define __DMB() ((void)0)
#define __HAL_TIM_SET_COMPARE(timer, channel, value) \
    test_tim_set_compare((timer), (channel), (value))
#define __HAL_TIM_CLEAR_FLAG(timer, flags) test_tim_clear_flags((timer), (flags))

void test_tim_set_compare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value);
void HAL_GPIO_WritePin(GPIO_TypeDef *gpio, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *gpio, uint16_t pin);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_GenerateEvent(TIM_HandleTypeDef *timer, uint32_t event);
void test_tim_clear_flags(TIM_HandleTypeDef *timer, uint32_t flags);
HAL_StatusTypeDef HAL_TIM_IC_Start(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_IC_Start_IT(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_IC_Stop(TIM_HandleTypeDef *timer, uint32_t channel);
uint32_t HAL_TIM_ReadCapturedValue(const TIM_HandleTypeDef *timer, uint32_t channel);
uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
void __DSB(void);
void __NOP(void);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *dma);
void HAL_TIM_IRQHandler(TIM_HandleTypeDef *timer);
void HAL_UART_IRQHandler(UART_HandleTypeDef *uart);
void HAL_GPIO_EXTI_IRQHandler(uint16_t pin);
HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_Encoder_Stop(TIM_HandleTypeDef *timer, uint32_t channel);
void HAL_GPIO_EXTI_Callback(uint16_t pin);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t size);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart);

#endif
