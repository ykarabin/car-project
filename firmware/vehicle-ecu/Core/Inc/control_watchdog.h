#ifndef VEHICLE_CONTROL_WATCHDOG_H
#define VEHICLE_CONTROL_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

/* 32 kHz LSI / 32, reload 499: nominal 500 ms (about 471..543 ms over
 * the STM32G473 specified 29.5..34 kHz LSI range, plus clock phasing). */
#define CONTROL_WATCHDOG_NOMINAL_MS UINT32_C(500)

/* Call once after HAL tick/clock setup, before starting RTOS/motor outputs.
 * This module owns IWDG registers; CubeMX HAL IWDG is not started separately. */
bool control_watchdog_start(void);
/* Only the default task calls this, after a complete control/logging pass.
 * No refresh from IRQ, idle hook, timer or inside the Flash operation. */
void control_watchdog_feed(void);
bool control_watchdog_was_reset(void);
uint32_t control_watchdog_reset_flags(void);

#endif
