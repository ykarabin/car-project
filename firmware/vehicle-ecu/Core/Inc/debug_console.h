#ifndef VEHICLE_DEBUG_CONSOLE_H
#define VEHICLE_DEBUG_CONSOLE_H

#include <stdint.h>

/* Logger task only: output-only and nonblocking, no input or IRQ logging.
 * The timestamp is the capture time, not the time a queued line is printed. */
void debug_console_init(void);
void debug_console_printf_at(uint32_t captured_at_ms, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#endif
