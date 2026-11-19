#ifndef VEHICLE_CONTROL_WATCHDOG_H
#define VEHICLE_CONTROL_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

/* Nominal timeout; actual timing depends on the LSI clock. */
#define CONTROL_WATCHDOG_NOMINAL_MS UINT32_C(500)

/* Start once after HAL clocks/tick, before enabling motor outputs. */
bool control_watchdog_start(void);
/* Refresh only after the control task completes an iteration. */
void control_watchdog_feed(void);
bool control_watchdog_was_reset(void);
uint32_t control_watchdog_reset_flags(void);

#endif
