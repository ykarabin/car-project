#include "remote_uart.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

#define REMOTE_UART UART_NUM_2

esp_err_t remote_uart_start(void)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_REMOTE_UART_TX_GPIO) ||
        (CONFIG_REMOTE_UART_TX_GPIO == CONFIG_ELRS_RX_GPIO)) {
        return ESP_ERR_INVALID_ARG;
    }
    const uart_config_t config = {
        .baud_rate = REMOTE_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };
    esp_err_t error = uart_param_config(REMOTE_UART, &config);
    if (error == ESP_OK) {
        error = uart_set_pin(REMOTE_UART, CONFIG_REMOTE_UART_TX_GPIO,
                             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (error == ESP_OK) {
        error = uart_driver_install(REMOTE_UART, 256, 0, 0, NULL, 0);
    }
    return error;
}

esp_err_t remote_uart_send(const remote_command_t *command)
{
    char line[REMOTE_LINE_SIZE];
    const int length = remote_command_format(line, sizeof(line), command);
    if (length < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    /* No TX queue: finish each short line before taking the next RC sample. */
    if (uart_write_bytes(REMOTE_UART, line, (size_t)length) != length) {
        return ESP_FAIL;
    }
    return uart_wait_tx_done(REMOTE_UART, pdMS_TO_TICKS(REMOTE_SEND_PERIOD_MS));
}
