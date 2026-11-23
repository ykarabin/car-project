#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "main.h"
#include "motor_config.h"
#include "motor_safety.h"
#include "stm32g4xx_it.h"

GPIO_TypeDef test_gpiod;
TIM_TypeDef test_tim1;
RCC_TypeDef test_rcc;
EXTI_TypeDef test_exti;
DMA_HandleTypeDef hdma_adc1;
TIM_HandleTypeDef htim4;
TIM_HandleTypeDef htim6;
UART_HandleTypeDef huart2;

static uint32_t s_mask;
static uint16_t s_pulse_on_wake;
static unsigned s_wake_writes;
static uint32_t s_now = 1000U;

uint32_t HAL_GetTick(void)
{
    return s_now;
}

#define SLP_PINS ((uint32_t)LEFT_MOTOR_SLP_Pin | RIGHT_MOTOR_SLP_Pin | STEERING_MOTOR_SLP_Pin)
#define FLT_PINS ((uint32_t)LEFT_MOTOR_FLT_Pin | RIGHT_MOTOR_FLT_Pin | STEERING_MOTOR_FLT_Pin)

uint32_t __get_PRIMASK(void)
{
    return s_mask;
}
void __disable_irq(void)
{
    s_mask = 1U;
}
void __set_PRIMASK(uint32_t mask)
{
    s_mask = mask;
}
void __DSB(void)
{
    GPIOD->ODR &= ~(GPIOD->BSRR >> 16U);
    GPIOD->BSRR = 0U;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *gpio, uint16_t pins, GPIO_PinState state)
{
    assert(s_mask == 1U);
    assert(gpio == GPIOD && state == GPIO_PIN_SET);
    assert(((uint32_t)pins & ~SLP_PINS) == 0U);
    assert(motor_safety_faults() == 0U);
    ++s_wake_writes;
    gpio->ODR |= pins;
    /* A short physical pulse can end before IRQs are unmasked. EXTI keeps
     * the falling edge pending even though the pin has already returned HIGH. */
    EXTI->PR1 |= s_pulse_on_wake;
}

void HAL_GPIO_EXTI_IRQHandler(uint16_t pin)
{
    if ((EXTI->PR1 & pin) != 0U) {
        EXTI->PR1 &= ~(uint32_t)pin;
        HAL_GPIO_EXTI_Callback(pin);
    }
}

/* Unrelated vectors are linked to exercise the production EXTI dispatcher. */
_Noreturn void fault_shutdown_halt(void)
{
    assert(false);
    for (;;) {
    }
}
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *dma)
{
    (void)dma;
    assert(false);
}
void HAL_TIM_IRQHandler(TIM_HandleTypeDef *timer)
{
    (void)timer;
    assert(false);
}
void HAL_UART_IRQHandler(UART_HandleTypeDef *uart)
{
    (void)uart;
    assert(false);
}
void remote_uart_capture_irq(UART_HandleTypeDef *uart)
{
    (void)uart;
    assert(false);
}
void steering_encoder_irq(void)
{
    assert(false);
}

static void assert_off(void)
{
    assert((GPIOD->ODR & SLP_PINS) == 0U);
    assert((GPIOD->ODR & GPIO_PIN_15) != 0U); /* Unrelated DIR is preserved. */
    assert((TIM1->BDTR & TIM_BDTR_MOE) == 0U);
    assert((TIM1->BDTR & TIM_BDTR_OSSI) != 0U);
    assert(TIM1->CR1 == TIM_CR1_UDIS); /* Shutdown needs no timer update. */
}

static void assert_latched(uint32_t expected)
{
    assert(motor_safety_faults() == expected);
    assert_off();
    GPIOD->IDR = FLT_PINS;
    EXTI->PR1 = 0U;
    const unsigned writes = s_wake_writes;
    assert(!motor_safety_wake(true, true));
    assert(!motor_safety_wake(false, true));
    assert(motor_safety_poll() == expected);
    assert(s_wake_writes == writes);
    assert_off();
    motor_safety_early_off();
    assert(motor_safety_faults() == expected); /* Early-off is not a rearm API. */
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    RCC->AHB2ENR = RCC_AHB2ENR_GPIODEN;
    RCC->APB2ENR = RCC_APB2ENR_TIM1EN;
    GPIOD->IDR = FLT_PINS;
    GPIOD->ODR = GPIO_PIN_15;
    TIM1->BDTR = TIM_BDTR_MOE | TIM_BDTR_OSSI;
    TIM1->CR1 = TIM_CR1_UDIS;
    assert(motor_safety_faults() == 0U);

    if (strcmp(argv[1], "normal") == 0) {
        EXTI->PR1 = LEFT_ENCODER_Z_Pin | RIGHT_ENCODER_Z_Pin;
        EXTI9_5_IRQHandler();
        assert(EXTI->PR1 == 0U); /* Reserved Z flags are cleared without application work. */
        HAL_GPIO_EXTI_Callback(GPIO_PIN_2);
        assert(motor_safety_poll() == 0U);
        assert(motor_safety_wake(true, false));
        assert((GPIOD->ODR & SLP_PINS) == (LEFT_MOTOR_SLP_Pin | RIGHT_MOTOR_SLP_Pin));
        GPIOD->ODR &= ~SLP_PINS;
        s_mask = 1U; /* A caller's existing critical section must remain held. */
        assert(motor_safety_poll() == 0U);
        assert(motor_safety_wake(false, true));
        assert(s_mask == 1U);
        assert((GPIOD->ODR & SLP_PINS) == STEERING_MOTOR_SLP_Pin);
        assert(motor_safety_faults() == 0U);
    } else if (strcmp(argv[1], "startup_low") == 0) {
        GPIOD->IDR &= ~(uint32_t)RIGHT_MOTOR_FLT_Pin;
        assert(!motor_safety_wake(true, true));
        assert(s_wake_writes == 0U && motor_safety_faults() == 0U);
        s_now += MOTOR_FAULT_CONFIRM_MS - 1U;
        assert(motor_safety_poll() == 0U);
        ++s_now;
        assert(motor_safety_poll() == MOTOR_FAULT_RIGHT);
        const motor_fault_snapshot_t first = motor_safety_first_fault();
        assert(first.first == MOTOR_FAULT_RIGHT && first.slp_high == 0U);
        assert(first.flt_low == MOTOR_FAULT_RIGHT && first.pending == 0U);
        assert(first.at_ms == s_now && first.wake_age_ms == UINT32_MAX && first.wake_mask == 0U);
        assert_latched(MOTOR_FAULT_RIGHT);
    } else if (strcmp(argv[1], "short_low") == 0) {
        assert(motor_safety_wake(true, true));
        GPIOD->IDR &= ~(uint32_t)LEFT_MOTOR_FLT_Pin;
        EXTI->PR1 = LEFT_MOTOR_FLT_Pin;
        EXTI15_10_IRQHandler();
        s_now += MOTOR_FAULT_CONFIRM_MS - 1U;
        assert(motor_safety_poll() == 0U);
        GPIOD->IDR = FLT_PINS;
        assert(motor_safety_poll() == 0U);
        s_now += 10U;
        assert(motor_safety_poll() == 0U);
        assert((GPIOD->ODR & SLP_PINS) == SLP_PINS);
    } else if (strcmp(argv[1], "pending_dedup") == 0) {
        assert(motor_safety_wake(true, true));
        s_now += MOTOR_FAULT_TRANSITION_MS + 1U;
        for (uint32_t i = 1U; i < MOTOR_FAULT_PULSE_LIMIT; ++i) {
            EXTI->PR1 = LEFT_MOTOR_FLT_Pin;
            assert(motor_safety_poll() == 0U);
            assert(motor_safety_poll() == 0U); /* Same pending bit, not new pulses. */
            EXTI15_10_IRQHandler();
            assert(motor_safety_poll() == 0U); /* Callback must not count it twice. */
        }
        EXTI->PR1 = LEFT_MOTOR_FLT_Pin;
        assert(motor_safety_poll() == MOTOR_FAULT_LEFT);
        assert_latched(MOTOR_FAULT_LEFT);
    } else if (strcmp(argv[1], "pulse_train") == 0) {
        s_now = UINT32_MAX - 2U;
        assert(motor_safety_wake(true, true));
        s_now = 0U; /* Beyond the transition window, across wrap. */
        for (uint32_t i = 1U; i < MOTOR_FAULT_PULSE_LIMIT; ++i) {
            EXTI->PR1 = LEFT_MOTOR_FLT_Pin;
            EXTI15_10_IRQHandler();
            assert(motor_safety_poll() == 0U);
            ++s_now;
        }
        s_now = 2U;
        EXTI->PR1 = LEFT_MOTOR_FLT_Pin | RIGHT_MOTOR_FLT_Pin;
        EXTI15_10_IRQHandler();
        const motor_fault_snapshot_t first = motor_safety_first_fault();
        assert(first.first == MOTOR_FAULT_LEFT && first.slp_high == 7U);
        assert(first.flt_low == 0U);
        assert(first.pending == (MOTOR_FAULT_LEFT | MOTOR_FAULT_RIGHT));
        assert(first.at_ms == 2U && first.wake_age_ms == 5U && first.wake_mask == 7U);
        assert_latched(MOTOR_FAULT_LEFT);
        s_now += MOTOR_FAULT_TRANSITION_MS + 1U; /* Past shutdown transition. */
        for (uint32_t i = 0U; i < MOTOR_FAULT_PULSE_LIMIT; ++i) {
            motor_safety_fault_irq(STEERING_MOTOR_FLT_Pin);
        }
        s_mask = 1U;
        const motor_fault_snapshot_t later = motor_safety_first_fault();
        assert(s_mask == 1U && memcmp(&first, &later, sizeof(first)) == 0);
        assert_latched(MOTOR_FAULT_LEFT | MOTOR_FAULT_STEERING);
    } else if (strcmp(argv[1], "wake_pulse") == 0) {
        s_pulse_on_wake = LEFT_MOTOR_FLT_Pin | RIGHT_MOTOR_FLT_Pin;
        assert(motor_safety_wake(true, false));
        EXTI15_10_IRQHandler();
        assert(s_wake_writes == 1U && s_mask == 0U);
        assert(motor_safety_poll() == 0U);
        assert((GPIOD->ODR & SLP_PINS) == (LEFT_MOTOR_SLP_Pin | RIGHT_MOTOR_SLP_Pin));
    } else if (strcmp(argv[1], "rolling_window") == 0) {
        assert(motor_safety_wake(true, true));
        s_now += MOTOR_FAULT_TRANSITION_MS + 1U;
        /* Two sparse events expire. Three crossing a fixed-window boundary
         * still trip within the rolling window. */
        motor_safety_fault_irq(LEFT_MOTOR_FLT_Pin);
        s_now += MOTOR_FAULT_PULSE_WINDOW_MS;
        motor_safety_fault_irq(LEFT_MOTOR_FLT_Pin);
        assert(motor_safety_poll() == 0U);
        s_now += MOTOR_FAULT_PULSE_WINDOW_MS - 1U;
        motor_safety_fault_irq(LEFT_MOTOR_FLT_Pin);
        assert(motor_safety_poll() == 0U);
        s_now += 2U;
        motor_safety_fault_irq(LEFT_MOTOR_FLT_Pin);
        assert(motor_safety_poll() == 0U);
        ++s_now;
        motor_safety_fault_irq(LEFT_MOTOR_FLT_Pin);
        assert_latched(MOTOR_FAULT_LEFT);
    } else if (strcmp(argv[1], "independent") == 0) {
        assert(motor_safety_wake(true, true));
        s_now += MOTOR_FAULT_TRANSITION_MS + 1U;
        for (uint32_t i = 1U; i < MOTOR_FAULT_PULSE_LIMIT; ++i) {
            EXTI->PR1 = LEFT_MOTOR_FLT_Pin | RIGHT_MOTOR_FLT_Pin | STEERING_MOTOR_FLT_Pin;
            EXTI15_10_IRQHandler();
            assert(motor_safety_poll() == 0U); /* Counts are not summed across drivers. */
        }
        motor_safety_fault_irq(RIGHT_MOTOR_FLT_Pin);
        assert_latched(MOTOR_FAULT_RIGHT);
    } else if (strcmp(argv[1], "restart_low") == 0) {
        assert(motor_safety_wake(true, true));
        s_now = UINT32_MAX;
        GPIOD->IDR &= ~(uint32_t)LEFT_MOTOR_FLT_Pin;
        EXTI->PR1 = LEFT_MOTOR_FLT_Pin;
        EXTI15_10_IRQHandler();
        s_now = 0U;
        /* Another falling edge proves an intervening HIGH missed by polling. */
        EXTI->PR1 = LEFT_MOTOR_FLT_Pin;
        EXTI15_10_IRQHandler();
        s_now = 1U;
        assert(motor_safety_poll() == 0U);
        s_now = 2U;
        assert(motor_safety_poll() == MOTOR_FAULT_LEFT);
        assert_latched(MOTOR_FAULT_LEFT);
    } else if (strcmp(argv[1], "transition_repeat") == 0) {
        assert(motor_safety_wake(true, true));
        s_pulse_on_wake = STEERING_MOTOR_FLT_Pin;
        for (uint32_t i = 0U; i < 6U; ++i) {
            s_now += 100U;
            GPIOD->ODR &= ~(uint32_t)STEERING_MOTOR_SLP_Pin;
            EXTI->PR1 = STEERING_MOTOR_FLT_Pin;
            EXTI15_10_IRQHandler();                /* Brief sleep transition pulse. */
            assert(motor_safety_wake(true, true)); /* Brief wake pulse. */
            EXTI15_10_IRQHandler();
            assert(motor_safety_poll() == 0U);
        }
        s_now += MOTOR_FAULT_TRANSITION_MS + 1U;
        for (uint32_t i = 0U; i < MOTOR_FAULT_PULSE_LIMIT; ++i) {
            motor_safety_fault_irq(STEERING_MOTOR_FLT_Pin);
        }
        assert_latched(MOTOR_FAULT_STEERING); /* Steady-state burst still trips. */
    } else if (strcmp(argv[1], "transition_low") == 0) {
        s_now = UINT32_MAX - 1U;
        assert(motor_safety_wake(true, true));
        GPIOD->IDR &= ~(uint32_t)STEERING_MOTOR_FLT_Pin;
        EXTI->PR1 = STEERING_MOTOR_FLT_Pin;
        EXTI15_10_IRQHandler();
        ++s_now;
        assert(motor_safety_poll() == 0U);
        ++s_now;
        assert(motor_safety_poll() == MOTOR_FAULT_STEERING);
        assert_latched(MOTOR_FAULT_STEERING); /* No LOW blanking during wake. */
    } else {
        assert(false);
    }
    return 0;
}
