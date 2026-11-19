#ifndef MT6701_PWM_H
#define MT6701_PWM_H

#include <stdbool.h>
#include <stdint.h>

/* Decode MT6701 PWM captured at 10 MHz into raw 0..4095.
 * Supports both nominal frame rates; invalid input leaves the output unchanged. */
bool mt6701_pwm_decode(uint32_t period_ticks, uint32_t high_ticks,
                      uint16_t *raw);

#endif
