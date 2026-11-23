#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "debug_console.h"
#include "SEGGER_RTT.h"

SEGGER_RTT_CB _SEGGER_RTT;
static uint32_t s_tick;
static char s_line[256];
static unsigned s_length;
static unsigned s_writes;
static int s_accept = 1;

void SEGGER_RTT_Init(void)
{
    _SEGGER_RTT.MaxNumDownBuffers = 1;
}
unsigned SEGGER_RTT_Write(unsigned channel, const void *bytes, unsigned length)
{
    assert(channel == 0U && length < sizeof(s_line));
    ++s_writes;
    if (!s_accept) {
        return 0U; /* Model NO_BLOCK_SKIP without a consuming host. */
    }
    (void)memcpy(s_line, bytes, length);
    s_line[length] = '\0';
    s_length = length;
    return length;
}

int main(void)
{
    debug_console_init();
    assert(_SEGGER_RTT.MaxNumDownBuffers == 0);
    s_tick = 12500U;
    debug_console_printf_at(s_tick, "EVENT RC %s\r\n", "OK");
    assert(strcmp(s_line, "[12500] EVENT RC OK\r\n") == 0);
    assert(s_writes == 1U);
    debug_console_printf_at(s_tick, "WARN UART ne=%u", 1U);
    assert(strcmp(s_line, "[12500] WARN UART ne=1\r\n") == 0);
    s_tick = UINT32_MAX;
    debug_console_printf_at(s_tick, "BOOT");
    assert(strcmp(s_line, "[4294967295] BOOT\r\n") == 0);
    s_tick = 0U;
    debug_console_printf_at(s_tick, "BOOT\n");
    assert(strcmp(s_line, "[0] BOOT\r\n") == 0);

    char payload[512];
    (void)memset(payload, 'x', sizeof(payload));
    payload[249] = '\0'; /* Largest payload with [0] prefix and CRLF. */
    debug_console_printf_at(s_tick, "%s", payload);
    assert(s_length == 255U && strcmp(&s_line[s_length - 5U], "xxx\r\n") == 0);
    payload[249] = 'x';
    payload[sizeof(payload) - 1U] = '\0';
    debug_console_printf_at(s_tick, "%s", payload);
    assert(s_length == 255U && strcmp(&s_line[s_length - 5U], "...\r\n") == 0);

    s_accept = 0;
    const unsigned before = s_writes;
    debug_console_printf_at(s_tick, "Skipped while full");
    assert(s_writes == before + 1U); /* No retry/wait loop. */
    s_accept = 1;
    debug_console_printf_at(s_tick, "Resumed");
    assert(strcmp(s_line, "[0] Resumed\r\n") == 0);
    const unsigned writes = s_writes;
    debug_console_printf_at(s_tick, "%s", "");
    assert(s_writes == writes);
    return 0;
}
