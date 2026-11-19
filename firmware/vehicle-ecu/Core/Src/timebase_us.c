#include "timebase_us.h"

#include "stm32g4xx_hal.h"

uint32_t timebase_us_now(void)
{
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    uint32_t milliseconds = HAL_GetTick();
    uint32_t fraction = TIM6->CNT;
    /* Account for overflow before its ISR; leave UIF clearing to HAL. */
    if ((TIM6->SR & TIM_SR_UIF) != 0U) {
        ++milliseconds;
        fraction = TIM6->CNT;
    }
    __set_PRIMASK(saved_mask);
    return milliseconds * 1000U + fraction;
}
