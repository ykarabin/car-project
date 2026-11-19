#ifndef VEHICLE_TIMEBASE_US_H
#define VEHICLE_TIMEBASE_US_H

#include <stdint.h>

/* Task context, after HAL clock initialization. Combines the existing 1 ms
 * HAL tick with TIM6's 1 MHz, ARR=999 counter. No timer reconfiguration.
 * Unsigned microseconds wrap naturally about every 71.6 minutes.
 * The HAL tick must be running; interrupt masking must remain below 1 ms. */
uint32_t timebase_us_now(void);

#endif
