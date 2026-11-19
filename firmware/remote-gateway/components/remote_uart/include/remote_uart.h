#ifndef REMOTE_GATEWAY_UART_H
#define REMOTE_GATEWAY_UART_H

#include "esp_err.h"
#include "remote_protocol.h"

esp_err_t remote_uart_start(void);
esp_err_t remote_uart_send(const remote_command_t *command);

#endif
