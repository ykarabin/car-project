#undef NDEBUG
#include <assert.h>
#include <stdint.h>

#include "stm32g4xx_hal.h"
#include "timebase_us.h"

TIM_TypeDef test_tim6;
static uint32_t s_tick;
static uint32_t s_mask;

uint32_t HAL_GetTick(void)
{
    assert(s_mask == 1U);
    return s_tick;
}
uint32_t __get_PRIMASK(void) { return s_mask; }
void __disable_irq(void) { s_mask = 1U; }
void __set_PRIMASK(uint32_t mask) { s_mask = mask; }

int main(void)
{
    s_tick = 42U;
    TIM6->CNT = 123U;
    TIM6->SR = 0U;
    s_mask = 0U;
    assert(timebase_us_now() == 42123U);
    assert(s_mask == 0U);
    s_mask = 1U;
    assert(timebase_us_now() == 42123U);
    assert(s_mask == 1U);

    /* Hardware rolled over, but the pending HAL ISR has not run yet. */
    TIM6->CNT = 7U;
    TIM6->SR = TIM_SR_UIF;
    assert(timebase_us_now() == 43007U);
    assert(TIM6->SR == TIM_SR_UIF && s_tick == 42U); /* Read-only helper. */
    s_tick = 43U;
    TIM6->CNT = 8U;
    TIM6->SR = 0U;
    assert(timebase_us_now() == 43008U);

    s_tick = UINT32_MAX;
    TIM6->CNT = 999U;
    assert(timebase_us_now() == UINT32_MAX);
    s_tick = 0U;
    TIM6->CNT = 0U;
    assert(timebase_us_now() == 0U);
    s_tick = UINT32_MAX;
    TIM6->CNT = 0U;
    TIM6->SR = TIM_SR_UIF;
    assert(timebase_us_now() == 0U);
    return 0;
}
