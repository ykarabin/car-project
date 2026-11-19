#include "motor_safety.h"

#include "main.h"

#define SLP_PINS ((uint32_t)LEFT_MOTOR_SLP_Pin | (uint32_t)RIGHT_MOTOR_SLP_Pin | \
                  (uint32_t)STEERING_MOTOR_SLP_Pin)
#define FLT_PINS ((uint32_t)LEFT_MOTOR_FLT_Pin | (uint32_t)RIGHT_MOTOR_FLT_Pin | \
                  (uint32_t)STEERING_MOTOR_FLT_Pin)
#define SLP_MODE_MASK ((UINT32_C(3) << 16U) | (UINT32_C(3) << 18U) | (UINT32_C(3) << 20U))
#define SLP_OUTPUT_MODE ((UINT32_C(1) << 16U) | (UINT32_C(1) << 18U) | (UINT32_C(1) << 20U))

/* Board-bound register access: all SLP/FLT pins are on GPIOD. */
_Static_assert(LEFT_MOTOR_SLP_Pin == GPIO_PIN_8 && RIGHT_MOTOR_SLP_Pin == GPIO_PIN_9 &&
               STEERING_MOTOR_SLP_Pin == GPIO_PIN_10, "SLP GPIO mode masks");

static volatile uint32_t s_latched_faults;

static void cut_outputs(void)
{
    GPIOD->BSRR = SLP_PINS << 16U;
    if ((RCC->APB2ENR & RCC_APB2ENR_TIM1EN) != 0U) {
        TIM1->BDTR &= ~TIM_BDTR_MOE;
    }
    __DSB();
}

void motor_safety_early_off(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIODEN;
    /* Read-back provides the peripheral clock enable propagation delay. */
    const volatile uint32_t clock_enabled = RCC->AHB2ENR;
    (void)clock_enabled;
    cut_outputs();
    GPIOD->OTYPER &= ~SLP_PINS;
    GPIOD->MODER = (GPIOD->MODER & ~SLP_MODE_MASK) | SLP_OUTPUT_MODE;
    __DSB();
}

static uint32_t fault_bits(uint32_t pins)
{
    uint32_t faults = 0U;
    if ((pins & LEFT_MOTOR_FLT_Pin) != 0U) {
        faults |= MOTOR_FAULT_LEFT;
    }
    if ((pins & RIGHT_MOTOR_FLT_Pin) != 0U) {
        faults |= MOTOR_FAULT_RIGHT;
    }
    if ((pins & STEERING_MOTOR_FLT_Pin) != 0U) {
        faults |= MOTOR_FAULT_STEERING;
    }
    return faults;
}

/* Caller holds PRIMASK. Do not clear EXTI pending bits: HAL owns them. */
static uint32_t sample_faults(void)
{
    s_latched_faults |= fault_bits(((~GPIOD->IDR) | EXTI->PR1) & FLT_PINS);
    if (s_latched_faults != 0U) {
        cut_outputs();
    }
    return s_latched_faults;
}

void motor_safety_fault_irq(uint16_t pin)
{
    const uint32_t faults = fault_bits(pin);
    if (faults == 0U) {
        return;
    }
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    s_latched_faults |= faults;
    cut_outputs();
    __set_PRIMASK(saved_mask);
}

uint32_t motor_safety_poll(void)
{
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    const uint32_t faults = sample_faults();
    __set_PRIMASK(saved_mask);
    return faults;
}

uint32_t motor_safety_faults(void)
{
    return s_latched_faults;
}

bool motor_safety_wake(bool rear_enabled, bool steering_enabled)
{
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    bool allowed = sample_faults() == 0U;
    if (allowed) {
        const uint16_t pins = (uint16_t)((rear_enabled ? LEFT_MOTOR_SLP_Pin | RIGHT_MOTOR_SLP_Pin : 0U) |
                                         (steering_enabled ? STEERING_MOTOR_SLP_Pin : 0U));
        HAL_GPIO_WritePin(GPIOD, pins, GPIO_PIN_SET);
        allowed = sample_faults() == 0U;
    }
    __set_PRIMASK(saved_mask);
    return allowed;
}
