#undef NDEBUG
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elrs.h"
#include "gateway_test_fakes.h"

static jmp_buf done;
static unsigned poll_count, step, published, read_count, warnings;
static int64_t now = INT64_C(1000000), last_warning;
static TaskFunction_t receiver;
static int uart_queue, sample_queue;
static elrs_sample_t latest;
static uint8_t frame[26];
static bool running;
static char warning[256];

const char *esp_err_to_name(esp_err_t error)
{
    return error == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}
_Noreturn void test_abort_error(esp_err_t error)
{
    (void)error;
    assert(false);
    abort();
}
void test_log(bool warn, const char *tag, const char *format, ...)
{
    assert(strcmp(tag, "elrs") == 0);
    if (!warn) return;
    assert(warnings == 0U || now - last_warning >= INT64_C(1000000));
    last_warning = now;
    ++warnings;
    va_list args;
    va_start(args, format);
    (void)vsnprintf(warning, sizeof(warning), format, args);
    va_end(args);
}
int64_t esp_timer_get_time(void) { return now; }
esp_err_t uart_param_config(int uart, const uart_config_t *config)
{
    assert(uart == UART_NUM_1 && config->baud_rate == 420000);
    return ESP_OK;
}
esp_err_t uart_set_pin(int uart, int tx, int rx, int rts, int cts)
{
    (void)tx; (void)rts; (void)cts;
    assert(uart == UART_NUM_1 && rx == CONFIG_ELRS_RX_GPIO);
    return ESP_OK;
}
esp_err_t gpio_set_pull_mode(int pin, int mode)
{
    assert(pin == CONFIG_ELRS_RX_GPIO && mode == GPIO_PULLUP_ONLY);
    return ESP_OK;
}
esp_err_t uart_driver_install(int uart, int rx, int tx, int length, QueueHandle_t *queue, int flags)
{
    (void)rx; (void)tx; (void)length; (void)flags;
    assert(uart == UART_NUM_1);
    *queue = &uart_queue;
    return ESP_OK;
}
esp_err_t uart_driver_delete(int uart) { (void)uart; assert(false); return ESP_FAIL; }
BaseType_t xTaskCreate(TaskFunction_t task, const char *name, uint32_t stack,
                       void *argument, UBaseType_t priority, void *handle)
{
    (void)name; (void)stack; (void)argument; (void)priority; (void)handle;
    receiver = task;
    return pdPASS;
}
QueueHandle_t xQueueCreateStatic(UBaseType_t length, size_t size, uint8_t *storage, StaticQueue_t *control)
{
    (void)storage; (void)control;
    assert(length == 1U && size == sizeof(latest));
    return &sample_queue;
}
void vQueueDelete(QueueHandle_t queue) { (void)queue; assert(false); }
BaseType_t xQueueReset(QueueHandle_t queue) { assert(queue == &uart_queue); return pdPASS; }
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t ticks)
{
    (void)ticks;
    assert(queue == &sample_queue);
    (void)memcpy(item, &latest, sizeof(latest));
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks)
{
    assert(queue == &uart_queue);
    if (ticks == 0U) return pdFALSE;
    if (poll_count == 240U) longjmp(done, 1);
    step = poll_count++;
    now += step == 17U ? 50000 : 10000;
    read_count = 0U;
    if (step == 1U || step == 15U) {
        ((uart_event_t *)item)->type = UART_FRAME_ERR;
        return pdTRUE;
    }
    return pdFALSE;
}
static bool parse_step(void)
{
    return step == 0U || step == 4U || step == 7U || step == 8U || step == 11U ||
           step == 12U || step == 14U || step == 16U || step == 18U || step == 19U || step >= 133U;
}
esp_err_t uart_get_buffered_data_len(int uart, size_t *length)
{
    assert(uart == UART_NUM_1);
    assert(parse_step() || step == 5U || step == 20U);
    *length = sizeof(frame);
    return step == 5U || step == 20U ? ESP_ERR_INVALID_STATE : ESP_OK;
}
esp_err_t uart_flush_input(int uart)
{
    assert(uart == UART_NUM_1);
    assert((step >= 1U && step <= 3U) || step == 6U || step == 9U || step == 10U ||
           step == 13U || step == 15U || step == 17U || (step >= 21U && step <= 132U));
    return step == 1U || step == 2U || step == 9U || (step >= 21U && step <= 131U) ? ESP_FAIL : ESP_OK;
}
int uart_read_bytes(int uart, void *buffer, uint32_t length, TickType_t ticks)
{
    (void)ticks;
    assert(uart == UART_NUM_1 && parse_step());
    if (step == 12U) return 0;
    if (step == 8U && read_count++ != 0U) return -1;
    const size_t count = step == 8U ? 13U : sizeof(frame);
    assert(count <= length);
    (void)memcpy(buffer, frame, count);
    return (int)count;
}
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item)
{
    assert(queue == &sample_queue);
    latest = *(const elrs_sample_t *)item;
    if (!running) return pdPASS;
    ++published;
    const bool valid = parse_step() && step != 8U && step != 12U;
    assert(latest.valid == valid);
    if (valid) {
        assert(latest.normalized[1] == 1000 && latest.received_at_us > 0);
    } else {
        assert(latest.received_at_us == 0 && latest.valid_since_us == 0);
        for (size_t i = 0U; i < ELRS_CHANNEL_COUNT; ++i) {
            assert(latest.normalized[i] == 0 && latest.raw[i] == 0U);
        }
    }
    if (step >= 132U) assert(latest.uart_errors == 121U);
    return pdPASS;
}
static void make_frame(void)
{
    frame[0] = 0xC8U; frame[1] = 24U; frame[2] = 0x16U;
    for (size_t ch = 0U; ch < ELRS_CHANNEL_COUNT; ++ch) {
        const uint16_t value = ch == 1U ? 1811U : 992U;
        for (size_t bit = 0U; bit < 11U; ++bit) {
            const size_t offset = ch * 11U + bit;
            frame[3U + offset / 8U] |= (uint8_t)(((value >> bit) & 1U) << (offset % 8U));
        }
    }
    uint32_t crc = 0U;
    for (size_t i = 2U; i < 25U; ++i) {
        crc ^= frame[i];
        for (size_t bit = 0U; bit < 8U; ++bit) {
            crc <<= 1U;
            if ((crc & 0x100U) != 0U) crc ^= 0x1D5U;
        }
    }
    frame[25] = (uint8_t)crc;
}
int main(void)
{
    make_frame();
    assert(elrs_start(7U) == ESP_OK && receiver != NULL);
    running = true;
    if (setjmp(done) == 0) receiver(NULL);
    assert(published == 240U && warnings == 3U);
    assert(strstr(warning, "recovery=ready") != NULL);
    elrs_sample_t sample;
    assert(elrs_get_sample(&sample) == ESP_OK && sample.valid);
    assert(sample.normalized[1] == 1000 && sample.uart_errors == 121U);
    return 0;
}
