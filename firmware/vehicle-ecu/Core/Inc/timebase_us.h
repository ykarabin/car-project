#ifndef VEHICLE_TIMEBASE_US_H
#define VEHICLE_TIMEBASE_US_H

#include <stdint.h>

/* Task-only microsecond clock from HAL tick and TIM6; wraps as uint32_t.
 * HAL tick must run and IRQ masking must stay below 1 ms. */
uint32_t timebase_us_now(void);

#endif
