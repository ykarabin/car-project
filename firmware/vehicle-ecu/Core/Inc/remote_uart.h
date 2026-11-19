#ifndef VEHICLE_REMOTE_UART_H
#define VEHICLE_REMOTE_UART_H

#include <stdbool.h>
#include <stdint.h>

#include "remote_protocol.h"
#include "stm32g4xx_hal.h"

#define REMOTE_UART_QUEUE_LENGTH 8U

typedef struct {
    remote_command_t command;
    uint32_t received_at_ms;
    bool valid;
} remote_uart_event_t;

/* Initialize once in the owner task. Storage is static, no heap allocation. */
bool remote_uart_init(UART_HandleTypeDef *uart);
/* Nonblocking task-side recovery and ordered event consumption. Overflow
 * preserves the queued prefix, then reports an invalid event at first loss. */
void remote_uart_service(void);
bool remote_uart_receive(remote_uart_event_t *event);
/* Cumulative error counts only; caller limits reporting frequency. */
void remote_uart_log_errors(void);
/* Record UART error flags before HAL clears them. No register writes. */
void remote_uart_capture_irq(UART_HandleTypeDef *uart);

#endif
