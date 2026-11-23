#include "mt6701_pwm.h"

#include <stddef.h>

/* MT6701 Rev.1.8, section 7.6: PWM frame timing.
 * https://www.magntek.com.cn/upload/pdf/202407/MT6701_Rev.1.8.pdf */
#define MT6701_FRAME_CLOCKS UINT32_C(4119)
#define MT6701_START_CLOCKS UINT32_C(16)
#define MT6701_MAX_RAW UINT32_C(4095)

bool mt6701_pwm_decode(uint32_t period_ticks, uint32_t high_ticks, uint16_t *raw)
{
    /* Accept both nominal frame rates with oscillator tolerance. */
    const bool fast_frame = (period_ticks >= 9000U) && (period_ticks <= 11000U);
    const bool slow_frame = (period_ticks >= 18000U) && (period_ticks <= 22000U);
    if ((raw == NULL) || (!fast_frame && !slow_frame) || (high_ticks == 0U) ||
        (high_ticks >= period_ticks)) {
        return false;
    }

    const uint32_t high_clocks =
        ((high_ticks * MT6701_FRAME_CLOCKS) + (period_ticks / 2U)) / period_ticks;
    if ((high_clocks < MT6701_START_CLOCKS) ||
        (high_clocks > (MT6701_START_CLOCKS + MT6701_MAX_RAW))) {
        return false;
    }
    *raw = (uint16_t)(high_clocks - MT6701_START_CLOCKS);
    return true;
}
