#ifndef MT6701_PWM_H
#define MT6701_PWM_H

#include <stdbool.h>
#include <stdint.h>

/* Non-inverted MT6701 PWM, measured with a 10 MHz timer (0.1 us/tick).
 * Supports nominal 497.2 Hz and 994.4 Hz frame rates. Output is unchanged
 * on invalid input. Raw output is 0..4095. No calibration or filtering is applied. */
bool mt6701_pwm_decode(uint32_t period_ticks, uint32_t high_ticks,
                      uint16_t *raw);

#endif
