#include "elrs.h"

#include <stddef.h>
#include <string.h>

#include "crsf.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define ELRS_UART UART_NUM_1
#define ELRS_RX_BUFFER_SIZE 2048
#define ELRS_UART_QUEUE_SIZE 20
#define ELRS_TASK_STACK_SIZE 3072U
#define ELRS_TASK_PRIORITY 5U
#define ELRS_POLL_MS 10U
#define ELRS_MAX_POLL_GAP_US INT64_C(50000)
#define ELRS_FRAME_GAP_US INT64_C(20000)
#define ELRS_SIGNAL_TIMEOUT_US ((int64_t)CONFIG_ELRS_SIGNAL_TIMEOUT_MS * INT64_C(1000))

_Static_assert(ELRS_CHANNEL_COUNT == CRSF_CHANNEL_COUNT, "Channel count mismatch");

static QueueHandle_t s_uart_queue;
static QueueHandle_t s_sample_queue;
static StaticQueue_t s_sample_queue_control;
static uint8_t s_sample_queue_storage[sizeof(elrs_sample_t)];
static uint16_t s_required_channel_mask;

static void invalidate_sample(elrs_sample_t *sample)
{
    sample->valid = false;
    sample->received_at_us = 0;
    sample->valid_since_us = 0;
    (void)memset(sample->raw, 0, sizeof(sample->raw));
    (void)memset(sample->normalized, 0, sizeof(sample->normalized));
}

/* Decode buffered CRSF frames and publish the latest RC sample. */
static void receiver_task(void *argument)
{
    (void)argument;
    crsf_parser_t parser = {.required_channel_mask = s_required_channel_mask};
    elrs_sample_t sample = {0};
    uint8_t buffer[128];
    int64_t previous_poll_us = esp_timer_get_time();
    int64_t last_data_us = 0;

    for (;;) {
        uart_event_t event;
        bool uart_error = false;

        if (xQueueReceive(s_uart_queue, &event, pdMS_TO_TICKS(ELRS_POLL_MS)) == pdTRUE) {
            /* Check pending UART errors before decoding buffered data. */
            for (size_t count = 0U; count < ELRS_UART_QUEUE_SIZE; ++count) {
                if ((event.type == UART_FIFO_OVF) || (event.type == UART_BUFFER_FULL) ||
                    (event.type == UART_FRAME_ERR) || (event.type == UART_PARITY_ERR) ||
                    (event.type == UART_BREAK)) {
                    uart_error = true;
                }
                if (((count + 1U) >= ELRS_UART_QUEUE_SIZE) ||
                    (xQueueReceive(s_uart_queue, &event, 0) != pdTRUE)) {
                    break;
                }
            }
        }

        const int64_t now_us = esp_timer_get_time();
        /* Discard buffered commands after a scheduling stall. */
        if (uart_error || ((now_us - previous_poll_us) >= ELRS_MAX_POLL_GAP_US)) {
            ESP_ERROR_CHECK(uart_flush_input(ELRS_UART));
            (void)xQueueReset(s_uart_queue);
            crsf_parser_reset(&parser);
            invalidate_sample(&sample);
            ++sample.uart_errors;
        } else {
            size_t buffered = 0U;
            ESP_ERROR_CHECK(uart_get_buffered_data_len(ELRS_UART, &buffered));
            if ((buffered > 0U) && ((now_us - last_data_us) >= ELRS_FRAME_GAP_US)) {
                crsf_parser_reset(&parser);
            }

            /* Process only the bytes buffered at the start of this pass. */
            while (buffered > 0U) {
                size_t to_read = buffered;
                if (to_read > sizeof(buffer)) {
                    to_read = sizeof(buffer);
                }

                const int received = uart_read_bytes(ELRS_UART, buffer, (uint32_t)to_read, 0);
                if (received <= 0) {
                    invalidate_sample(&sample);
                    ++sample.uart_errors;
                    break;
                }

                buffered -= (size_t)received;
                for (size_t index = 0U; index < (size_t)received; ++index) {
                    uint16_t channels[CRSF_CHANNEL_COUNT];
                    const crsf_result_t result = crsf_parser_push(&parser, buffer[index], channels);
                    if (result == CRSF_RESULT_CHANNELS) {
                        (void)memcpy(sample.raw, channels, sizeof(sample.raw));
                        for (size_t channel = 0U; channel < ELRS_CHANNEL_COUNT; ++channel) {
                            sample.normalized[channel] = crsf_normalize(channels[channel]);
                        }
                        /* Include UART buffering time in the sample age. */
                        sample.received_at_us = previous_poll_us;
                        if (!sample.valid) {
                            sample.valid_since_us = sample.received_at_us;
                        }
                        sample.valid = true;
                    } else if (result == CRSF_RESULT_INVALID_CHANNELS) {
                        (void)memcpy(sample.rejected_raw, channels, sizeof(sample.rejected_raw));
                        invalidate_sample(&sample);
                    } else {
                        /* Only valid channel frames refresh the command. */
                    }
                }
                last_data_us = now_us;
            }
        }

        previous_poll_us = now_us;
        sample.rc_frames = parser.rc_frames;
        sample.crc_errors = parser.crc_errors;
        sample.frame_errors = parser.frame_errors;
        sample.range_errors = parser.range_errors;
        if (sample.valid && ((now_us - sample.received_at_us) >= ELRS_SIGNAL_TIMEOUT_US)) {
            invalidate_sample(&sample);
        }
        (void)xQueueOverwrite(s_sample_queue, &sample);
    }
}

esp_err_t elrs_start(uint16_t required_channel_mask)
{
    if (s_sample_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((required_channel_mask == 0U) || !GPIO_IS_VALID_GPIO(CONFIG_ELRS_RX_GPIO)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uart_config_t uart_config = {
        .baud_rate = 420000,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    esp_err_t error = uart_param_config(ELRS_UART, &uart_config);
    if (error == ESP_OK) {
        error = uart_set_pin(ELRS_UART, UART_PIN_NO_CHANGE, CONFIG_ELRS_RX_GPIO,
                             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (error == ESP_OK) {
        error = gpio_set_pull_mode(CONFIG_ELRS_RX_GPIO, GPIO_PULLUP_ONLY);
    }
    if (error != ESP_OK) {
        return error;
    }

    error = uart_driver_install(ELRS_UART, ELRS_RX_BUFFER_SIZE, 0,
                                ELRS_UART_QUEUE_SIZE, &s_uart_queue, 0);
    if (error != ESP_OK) {
        return error;
    }

    s_sample_queue = xQueueCreateStatic(1U, sizeof(elrs_sample_t),
                                        s_sample_queue_storage, &s_sample_queue_control);
    const elrs_sample_t initial_sample = {0};
    (void)xQueueOverwrite(s_sample_queue, &initial_sample);
    s_required_channel_mask = required_channel_mask;

    if (xTaskCreate(receiver_task, "elrs_rx", ELRS_TASK_STACK_SIZE, NULL,
                    ELRS_TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(s_sample_queue);
        s_sample_queue = NULL;
        ESP_ERROR_CHECK(uart_driver_delete(ELRS_UART));
        s_uart_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t elrs_get_sample(elrs_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_sample_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueuePeek(s_sample_queue, sample, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sample->valid &&
        ((esp_timer_get_time() - sample->received_at_us) >= ELRS_SIGNAL_TIMEOUT_US)) {
        invalidate_sample(sample);
    }

    return ESP_OK;
}
