#include "timebase_us.h"

#include "stm32g4xx_hal.h"

uint32_t timebase_us_now(void)
{
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    uint32_t milliseconds = HAL_GetTick();
    uint32_t fraction = TIM6->CNT;
    /* Overflow can precede its ISR, or happen between the first CNT read
     * and this flag read. Re-read the new period without clearing UIF;
     * the normal HAL interrupt remains responsible for incrementing uwTick. */
    if ((TIM6->SR & TIM_SR_UIF) != 0U) {
        ++milliseconds;
        fraction = TIM6->CNT;
    }
    __set_PRIMASK(saved_mask);
    return milliseconds * 1000U + fraction;
}
