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

/* Initialize once in the control task; storage is static. */
bool remote_uart_init(UART_HandleTypeDef *uart);
/* Nonblocking recovery and FIFO consumption. Overflow is reported after queued events. */
void remote_uart_service(void);
bool remote_uart_receive(remote_uart_event_t *event);
/* Publish cumulative errors; the caller sets the reporting rate. */
void remote_uart_log_errors(void);
/* Capture error flags before HAL clears them. */
void remote_uart_capture_irq(UART_HandleTypeDef *uart);

#endif
