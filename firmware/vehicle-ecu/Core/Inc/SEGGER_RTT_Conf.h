#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

#define SEGGER_RTT_MAX_NUM_UP_BUFFERS 1
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS 1
#define BUFFER_SIZE_UP 2048
/* RTT requires a down buffer; the console exposes output only. */
#define BUFFER_SIZE_DOWN 2
#define SEGGER_RTT_MODE_DEFAULT SEGGER_RTT_MODE_NO_BLOCK_SKIP
/* Use the portable C implementation. */
#define RTT_USE_ASM 0

#endif
