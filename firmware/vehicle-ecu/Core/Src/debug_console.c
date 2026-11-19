#include "debug_console.h"

#include <stdarg.h>
#include <stdio.h>
#include <inttypes.h>

#include "SEGGER_RTT.h"

void debug_console_init(void)
{
    SEGGER_RTT_Init();
    /* The public control block advertises diagnostics only to the probe.
     * The library still needs storage for its default down descriptor. */
    _SEGGER_RTT.MaxNumDownBuffers = 0;
}

void debug_console_printf_at(uint32_t captured_at_ms, const char *format, ...)
{
    char buffer[256];
    /* Keep the C library's signed int result until error/range checks pass. */
    const int prefix = snprintf(buffer, sizeof(buffer), "[%" PRIu32 "] ", captured_at_ms);
    if ((prefix < 0) || ((size_t)prefix >= sizeof(buffer) - 3U)) {
        return;
    }
    const size_t offset = (size_t)prefix;
    /* Reserve CRLF and NUL even if the payload is too long. */
    const size_t capacity = sizeof(buffer) - offset - 2U;
    va_list arguments;
    va_start(arguments, format);
    const int count = vsnprintf(&buffer[offset], capacity, format, arguments);
    va_end(arguments);
    if (count < 0) {
        return;
    }
    if (count == 0) {
        return;
    }
    size_t used = (size_t)count;
    if (used >= capacity) {
        used = capacity - 1U;
        buffer[offset + used - 3U] = '.';
        buffer[offset + used - 2U] = '.';
        buffer[offset + used - 1U] = '.';
    }
    while ((used != 0U) && ((buffer[offset + used - 1U] == '\r') ||
                            (buffer[offset + used - 1U] == '\n'))) {
        --used;
    }
    buffer[offset + used] = '\r';
    buffer[offset + used + 1U] = '\n';
    buffer[offset + used + 2U] = '\0';
    const uint32_t length = (uint32_t)(offset + used + 2U);
    /* NO_BLOCK_SKIP writes the complete line or nothing. */
    (void)SEGGER_RTT_Write(0U, buffer, length);
}

