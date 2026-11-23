#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fault_shutdown.h"
#include "FreeRTOSConfig.h"
#include "main.h"
#include "remote_uart.h"
#include "steering_encoder.h"
#include "stm32g4xx_it.h"

uint32_t HAL_GetTick(void) { return 0U; }

GPIO_TypeDef test_gpiod;
TIM_TypeDef test_tim1;
RCC_TypeDef test_rcc;
EXTI_TypeDef test_exti;
DMA_HandleTypeDef hdma_adc1;
TIM_HandleTypeDef htim4;
TIM_HandleTypeDef htim6;
UART_HandleTypeDef huart2;

static jmp_buf s_halt_escape;
static bool s_masked;
static bool s_barrier;
static unsigned s_assert_evaluations;

void __disable_irq(void) { s_masked = true; }
uint32_t __get_PRIMASK(void) { return s_masked ? 1U : 0U; }
void __set_PRIMASK(uint32_t mask) { s_masked = mask != 0U; }

void __DSB(void)
{
    assert(s_masked);
    assert((RCC->AHB2ENR & RCC_AHB2ENR_GPIODEN) != 0U);
    /* Model the atomic GPIO reset write before observing the terminal loop. */
    GPIOD->ODR &= ~(GPIOD->BSRR >> 16U);
    GPIOD->BSRR = 0U;
    s_barrier = true;
}

void __NOP(void)
{
    assert(s_barrier); /* Escape only once the real halt loop has been reached. */
    longjmp(s_halt_escape, 1);
}

/* The fatal paths must not call HAL or the normal peripheral IRQ handlers. */
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *dma) { (void)dma; assert(false); }
void HAL_TIM_IRQHandler(TIM_HandleTypeDef *timer) { (void)timer; assert(false); }
void HAL_UART_IRQHandler(UART_HandleTypeDef *uart) { (void)uart; assert(false); }
void HAL_GPIO_EXTI_IRQHandler(uint16_t pin) { (void)pin; assert(false); }
void remote_uart_capture_irq(UART_HandleTypeDef *uart) { (void)uart; assert(false); }
void steering_encoder_irq(void) { assert(false); }
void HAL_GPIO_WritePin(GPIO_TypeDef *gpio, uint16_t pin, GPIO_PinState state)
{
    (void)gpio; (void)pin; (void)state; assert(false);
}

static void trigger_failed_assert(void)
{
    configASSERT(s_assert_evaluations++);
    assert(false); /* A failed assertion must never return to normal code. */
}

static void test_terminal_faults_disable_outputs(void)
{
    static void (*const entries[])(void) = {
        fault_shutdown_halt, NMI_Handler, HardFault_Handler, MemManage_Handler,
        BusFault_Handler, UsageFault_Handler, trigger_failed_assert
    };
    const uint32_t slp = (uint32_t)LEFT_MOTOR_SLP_Pin | (uint32_t)RIGHT_MOTOR_SLP_Pin |
                         (uint32_t)STEERING_MOTOR_SLP_Pin;
    for (size_t entry = 0U; entry < sizeof(entries) / sizeof(entries[0]); ++entry) {
        for (unsigned driving = 0U; driving < 2U; ++driving) {
            (void)memset(TIM1, 0, sizeof(*TIM1));
            GPIOD->ODR = driving != 0U ? UINT32_C(0xFFFF) : 0U;
            GPIOD->BSRR = 0U;
            GPIOD->MODER = UINT32_MAX;
            GPIOD->OTYPER = UINT32_C(0xFFFF);
            RCC->AHB2ENR = UINT32_C(0x40000000); /* GPIOD initially clock-gated. */
            RCC->APB2ENR = driving != 0U ? RCC_APB2ENR_TIM1EN : 0U;
            TIM1->BDTR = driving != 0U ? TIM_BDTR_MOE | UINT32_C(0x0400) : 0U;
            TIM1->CR1 = TIM_CR1_UDIS; /* Also stop when a CCR batch was interrupted. */
            for (size_t channel = 0U; channel < 3U; ++channel) {
                TIM1->compare[channel] = 8500U;
                TIM1->active_compare[channel] = 8500U;
            }
            s_masked = false;
            s_barrier = false;
            s_assert_evaluations = 0U;
            if (setjmp(s_halt_escape) == 0) {
                entries[entry]();
                assert(false);
            }
            assert(s_masked && s_barrier);
            assert((GPIOD->ODR & slp) == 0U);
            assert((RCC->AHB2ENR & RCC_AHB2ENR_GPIODEN) != 0U);
            assert((RCC->AHB2ENR & UINT32_C(0x40000000)) != 0U);
            assert((GPIOD->OTYPER & slp) == 0U);
            assert((GPIOD->OTYPER & ~slp) == (UINT32_C(0xFFFF) & ~slp));
            const uint32_t modes = (UINT32_C(3) << 16U) | (UINT32_C(3) << 18U) | (UINT32_C(3) << 20U);
            const uint32_t outputs = (UINT32_C(1) << 16U) | (UINT32_C(1) << 18U) | (UINT32_C(1) << 20U);
            assert((GPIOD->MODER & modes) == outputs);
            assert((GPIOD->MODER & ~modes) == (UINT32_MAX & ~modes));
            assert((GPIOD->ODR & ~slp) == (driving != 0U ? UINT32_C(0xFFFF) & ~slp : 0U));
            assert((TIM1->BDTR & TIM_BDTR_MOE) == 0U);
            assert((TIM1->BDTR & UINT32_C(0x0400)) == (driving != 0U ? UINT32_C(0x0400) : 0U));
            assert(TIM1->CR1 == TIM_CR1_UDIS); /* No update event was needed. */
            assert(TIM1->active_compare[0] == 8500U);
        }
    }
    assert(s_assert_evaluations == 1U);
}

int main(void)
{
    unsigned evaluated = 0U;
    if (true) configASSERT(++evaluated);
    else assert(false);
    assert(evaluated == 1U);
    test_terminal_faults_disable_outputs();
    return 0;
}
