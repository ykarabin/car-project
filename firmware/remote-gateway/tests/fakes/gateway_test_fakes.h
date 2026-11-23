#ifndef GATEWAY_TEST_FAKES_H
#define GATEWAY_TEST_FAKES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
const char *esp_err_to_name(esp_err_t error);
_Noreturn void test_abort_error(esp_err_t error);
void test_log(bool warning, const char *tag, const char *format, ...);
#define ESP_ERROR_CHECK(call) do { esp_err_t result_ = (call); if (result_ != ESP_OK) test_abort_error(result_); } while (0)
#define ESP_LOGI(tag, ...) test_log(false, tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) test_log(true, tag, __VA_ARGS__)

typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef void *QueueHandle_t;
typedef struct { unsigned unused; } StaticQueue_t;
typedef void (*TaskFunction_t)(void *);
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskDelayUntil(TickType_t *wake, TickType_t ticks);
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreate(TaskFunction_t task, const char *name, uint32_t stack,
                       void *argument, UBaseType_t priority, void *handle);
QueueHandle_t xQueueCreateStatic(UBaseType_t length, size_t size, uint8_t *storage, StaticQueue_t *control);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks);
BaseType_t xQueueReset(QueueHandle_t queue);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t ticks);
void vQueueDelete(QueueHandle_t queue);

#define GPIO_IS_VALID_GPIO(pin) ((pin) >= 0 && (pin) <= 48)
#define GPIO_PULLUP_ONLY 1
esp_err_t gpio_set_pull_mode(int pin, int mode);
#define UART_NUM_1 1
#define UART_NUM_2 2
#define UART_PIN_NO_CHANGE (-1)
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_FIFO_OVF 1
#define UART_BUFFER_FULL 2
#define UART_FRAME_ERR 3
#define UART_PARITY_ERR 4
#define UART_BREAK 5
typedef struct { int type; } uart_event_t;
typedef struct {
    int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk;
} uart_config_t;
esp_err_t uart_param_config(int uart, const uart_config_t *config);
esp_err_t uart_set_pin(int uart, int tx, int rx, int rts, int cts);
esp_err_t uart_driver_install(int uart, int rx_size, int tx_size, int queue_size,
                               QueueHandle_t *queue, int flags);
esp_err_t uart_driver_delete(int uart);
esp_err_t uart_flush_input(int uart);
esp_err_t uart_get_buffered_data_len(int uart, size_t *length);
int uart_read_bytes(int uart, void *buffer, uint32_t length, TickType_t ticks);
int64_t esp_timer_get_time(void);
#define CONFIG_ELRS_RX_GPIO 16
#define CONFIG_ELRS_SIGNAL_TIMEOUT_MS 200
#define CONFIG_PWM_RC_CH1_GPIO 5
#define CONFIG_PWM_RC_CH2_GPIO 6
#define CONFIG_PWM_RC_CH3_GPIO 7
#define CONFIG_REMOTE_UART_TX_GPIO 4
#endif
