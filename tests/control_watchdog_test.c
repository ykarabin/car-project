#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "control_watchdog.h"
#include "stm32g4xx_hal.h"

RCC_TypeDef test_rcc;
DBGMCU_TypeDef test_dbgmcu;
IWDG_TypeDef test_iwdg;

static uint32_t s_tick;
static unsigned s_tick_calls;
static unsigned s_registers_ready_after;

uint32_t HAL_GetTick(void)
{
    ++s_tick_calls;
    if ((s_registers_ready_after != 0U) && (s_tick_calls >= s_registers_ready_after)) {
        IWDG->SR = 0U;
    }
    return s_tick++;
}

static void test_start_and_reset_reason(void)
{
    const uint32_t flags = RCC_CSR_IWDGRSTF | UINT32_C(0x08000000);
    RCC->CSR = flags;
    DBGMCU->APB1FZR1 = UINT32_C(0x80);
    IWDG->SR = IWDG_SR_PVU | IWDG_SR_RVU | IWDG_SR_WVU;
    s_registers_ready_after = 4U;
    assert(control_watchdog_start());
    assert(s_tick_calls >= 4U && s_tick_calls < 100U);
    assert(control_watchdog_was_reset());
    assert(control_watchdog_reset_flags() == flags);
    assert((RCC->CSR & RCC_CSR_RMVF) != 0U);
    assert((DBGMCU->APB1FZR1 & DBGMCU_APB1FZR1_DBG_IWDG_STOP) != 0U);
    assert((DBGMCU->APB1FZR1 & UINT32_C(0x80)) != 0U);
    const uint32_t prescaler = UINT32_C(4) << IWDG->PR;
    /* Validate the actual configured deadline, including LSI tolerance. */
    assert(((IWDG->RLR + 1U) * prescaler * 1000U / 32000U) == 500U);
    assert(((IWDG->RLR + 1U) * prescaler * 1000U / 34000U) >= 470U);
    assert(((IWDG->RLR + 1U) * prescaler * 1000U / 29500U) <= 543U);
    assert(IWDG->WINR == UINT32_C(0xFFF));
    IWDG->KR = 0U;
    control_watchdog_feed();
    assert(IWDG->KR == UINT32_C(0xAAAA));
}

static void test_bounded_start_failure_across_tick_wrap(void)
{
    RCC->CSR = UINT32_C(0x08000000); /* Fresh reset without the IWDG flag. */
    s_tick = UINT32_MAX - 20U;
    s_tick_calls = 0U;
    s_registers_ready_after = 0U;
    IWDG->SR = IWDG_SR_RVU;
    assert(!control_watchdog_start());
    assert(s_tick_calls >= 100U && s_tick_calls <= 102U);
    assert(!control_watchdog_was_reset());
    assert(control_watchdog_reset_flags() == UINT32_C(0x08000000));
    IWDG->KR = 0U;
    control_watchdog_feed();
    assert(IWDG->KR == 0U); /* A failed start cannot be refreshed indefinitely. */
}

int main(void)
{
    IWDG->KR = 0U;
    control_watchdog_feed();
    assert(IWDG->KR == 0U);
    test_start_and_reset_reason();
    test_bounded_start_failure_across_tick_wrap();
    return 0;
}
