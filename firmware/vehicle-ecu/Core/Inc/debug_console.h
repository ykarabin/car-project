#ifndef VEHICLE_DEBUG_CONSOLE_H
#define VEHICLE_DEBUG_CONSOLE_H

#include <stdint.h>

/* Nonblocking output from the logger task, timestamped at event capture. */
void debug_console_init(void);
void debug_console_printf_at(uint32_t captured_at_ms, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#endif
