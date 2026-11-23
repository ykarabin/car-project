#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <setjmp.h>
#include <string.h>

#include "remote_uart.h"
#include "vehicle_log.h"
#include "debug_console.h"
#include "remote_command_monitor.h"
#include "queue.h"
#include "vehicle_app.h"
#include "vehicle_control.h"
#include "rear_encoders.h"
#include "task.h"

static USART_TypeDef s_usart;
static UART_HandleTypeDef s_uart = {.Instance = &s_usart};
static uint8_t *s_receive_byte;
static uint32_t s_now, s_mask;
static bool s_fail_arm, s_in_isr;
static unsigned s_arm_calls;
static char s_log[4096];
static jmp_buf s_app_exit;
static unsigned s_app_updates, s_feeds, s_delays;
static uint32_t s_fault;
static unsigned s_app_cycles, s_status_calls;
static bool s_inject_fault;
static bool s_fail_log;
static unsigned s_console_inits;

uint32_t HAL_GetTick(void)
{
    return s_now;
}
uint32_t __get_PRIMASK(void)
{
    return s_mask;
}
void __disable_irq(void)
{
    s_mask = 1U;
}
void __set_PRIMASK(uint32_t mask)
{
    s_mask = mask;
}

HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart)
{
    assert(s_in_isr || s_mask == 1U);
    uart->RxState = HAL_UART_STATE_READY;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t size)
{
    assert(s_in_isr || s_mask == 1U); /* Task check/rearm must be indivisible. */
    assert(size == 1U && uart == &s_uart);
    ++s_arm_calls;
    if (s_fail_arm) {
        return HAL_ERROR;
    }
    assert(uart->RxState == HAL_UART_STATE_READY);
    s_receive_byte = data;
    uart->RxState = HAL_UART_STATE_BUSY_RX;
    return HAL_OK;
}

QueueHandle_t xQueueCreateStatic(UBaseType_t length, UBaseType_t size, uint8_t *storage,
                                 StaticQueue_t *queue)
{
    *queue = (StaticQueue_t){.storage = storage, .length = length, .item_size = size};
    return queue;
}
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *wake)
{
    assert(s_in_isr);
    (void)wake;
    if (queue->count == queue->length) {
        return pdFALSE;
    }
    const size_t tail = (queue->head + queue->count) % queue->length;
    (void)memcpy(queue->storage + tail * queue->item_size, item, queue->item_size);
    ++queue->count;
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait)
{
    assert(s_mask == 1U && wait == 0U);
    if (queue->count == 0U) {
        return pdFALSE;
    }
    (void)memcpy(item, queue->storage + queue->head * queue->item_size, queue->item_size);
    queue->head = (queue->head + 1U) % queue->length;
    --queue->count;
    return pdTRUE;
}
/* Immediate consumer fake keeps the existing golden text assertions.
 * Queue ownership/copy/full/delayed timestamp are tested with real transport
 * separately in vehicle_log_test. Production control never formats. */
bool vehicle_log_publish(const vehicle_log_message_t *message)
{
    assert(!s_in_isr);
    if (s_fail_log) {
        return false;
    }
    vehicle_log_format(HAL_GetTick(), message);
    return true;
}
void vehicle_log_text(const char *text)
{
    const vehicle_log_message_t message = {.kind = VEHICLE_LOG_TEXT, .data.text = text};
    (void)vehicle_log_publish(&message);
}

void debug_console_printf_at(uint32_t captured_at_ms, const char *format, ...)
{
    (void)captured_at_ms;
    assert(!s_in_isr);
    const size_t used = strlen(s_log);
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(s_log + used, sizeof(s_log) - used, format, args);
    va_end(args);
    assert(length >= 0 && (size_t)length < sizeof(s_log) - used);
}

static void init(void)
{
    s_log[0] = '\0';
    s_fail_log = false;
    s_console_inits = 0U;
    s_now = 0U;
    s_mask = 0U;
    s_fail_arm = false;
    s_in_isr = false;
    s_arm_calls = 0U;
    s_usart.ISR = 0U;
    assert(remote_uart_init(&s_uart));
    assert(s_mask == 0U && s_uart.RxState == HAL_UART_STATE_BUSY_RX);
}
static void bytes(const char *text)
{
    s_in_isr = true;
    for (size_t i = 0U; text[i] != '\0'; ++i) {
        assert(s_uart.RxState == HAL_UART_STATE_BUSY_RX);
        *s_receive_byte = (uint8_t)text[i];
        s_uart.RxState = HAL_UART_STATE_READY;
        HAL_UART_RxCpltCallback(&s_uart);
    }
    s_in_isr = false;
}
static void command(uint8_t sequence)
{
    const remote_command_t command = {.steering = 23,
                                      .throttle = 123,
                                      .drive_mode = 1U,
                                      .status = REMOTE_STATUS_OK,
                                      .sequence = sequence};
    char line[REMOTE_LINE_SIZE];
    assert(remote_command_format(line, sizeof(line), &command) > 0);
    bytes(line);
}
static remote_uart_event_t receive(void)
{
    remote_uart_event_t event;
    assert(remote_uart_receive(&event));
    return event;
}
static void test_order_and_real_arrival_grace(void)
{
    init();
    s_now = 10U;
    command(1U);
    s_now = 20U;
    bytes("bad\n");
    s_now = 25U;
    command(2U);
    const remote_uart_event_t first = receive(), bad = receive(), last = receive();
    assert(first.valid && first.command.sequence == 1U && first.received_at_ms == 10U);
    assert(!bad.valid && bad.received_at_ms == 20U);
    assert(last.valid && last.command.sequence == 2U && last.received_at_ms == 25U);
    remote_uart_event_t empty;
    assert(!remote_uart_receive(&empty));
    remote_command_monitor_t monitor;
    remote_command_monitor_init(&monitor);
    assert(remote_command_monitor_accept(&monitor, &first.command, first.received_at_ms));
    remote_command_monitor_reject(&monitor, bad.received_at_ms);
    assert(remote_command_monitor_state(&monitor, 79U) == REMOTE_COMMAND_OK);
    assert(remote_command_monitor_state(&monitor, 80U) == REMOTE_COMMAND_INVALID);
    assert(remote_command_monitor_accept(&monitor, &last.command, last.received_at_ms));
    assert(remote_command_monitor_state(&monitor, 125U) == REMOTE_COMMAND_STALE);
}
static void test_overflow_preserves_prefix_and_reports_first_loss(void)
{
    init();
    for (unsigned i = 0U; i < REMOTE_UART_QUEUE_LENGTH; ++i) {
        s_now = i;
        command((uint8_t)i);
    }
    s_now = 40U;
    command(10U); /* First lost event. */
    s_now = 45U;
    bytes("bad\n");
    for (unsigned i = 0U; i < REMOTE_UART_QUEUE_LENGTH; ++i) {
        const remote_uart_event_t event = receive();
        assert(event.valid && event.command.sequence == i);
        if (i == 0U) {
            s_now = 50U;
            command(11U);
        } /* No reordering into freed slots. */
    }
    const remote_uart_event_t overflow = receive();
    assert(!overflow.valid && overflow.received_at_ms == 40U);
    s_now = 60U;
    command(12U);
    assert(receive().command.sequence == 12U);
    remote_uart_log_errors();
    assert(strstr(s_log, "fifo_lost=3") != NULL);
}
static void test_gap_error_and_rearm_recovery(void)
{
    init();
    bytes("RC,");
    s_now = REMOTE_SEND_PERIOD_MS;
    command(1U);
    assert(!receive().valid);
    assert(receive().valid);
    s_now = 30U;
    s_in_isr = true;
    HAL_UART_ErrorCallback(&s_uart);
    s_in_isr = false;
    const remote_uart_event_t error = receive();
    assert(!error.valid && error.received_at_ms == 30U);
    s_fail_arm = true;
    bytes("x");
    assert(!receive().valid && s_uart.RxState == HAL_UART_STATE_READY);
    remote_uart_service();
    assert(s_mask == 0U && s_uart.RxState == HAL_UART_STATE_READY);
    s_fail_arm = false;
    s_mask = 1U;
    remote_uart_service();
    assert(s_mask == 1U && s_uart.RxState == HAL_UART_STATE_BUSY_RX);
    s_mask = 0U;
    const unsigned calls = s_arm_calls;
    remote_uart_service();
    assert(s_arm_calls == calls); /* No task rearm of an already active RX. */
    s_now = 35U;
    command(2U);
    assert(receive().valid);
}
static void test_error_irq_diagnostics(void)
{
    init();
    s_log[0] = '\0';
    s_usart.ISR = USART_ISR_RTOF;
    remote_uart_capture_irq(&s_uart);
    remote_uart_log_errors();
    assert(s_log[0] == '\0'); /* RTO is not enabled and is not a UART error here. */
    s_usart.ISR = USART_ISR_NE;
    remote_uart_capture_irq(&s_uart);
    assert(s_usart.ISR == USART_ISR_NE); /* Pre-HAL hook reads, never clears flags. */
    remote_uart_log_errors();
    assert(strstr(s_log, "ne=1") != NULL);
}

/* Run the production application loop and transport together. The algorithms
 * already have their own suites; here check sequencing, FLT gating and feed. */
void debug_console_init(void)
{
    ++s_console_inits;
    s_log[0] = '\0';
}
void vehicle_control_init(void)
{
}
void rear_encoders_update(void)
{
}
wheel_speed_sample_t rear_encoders_get(void)
{
    return (wheel_speed_sample_t){0};
}
steering_encoder_sample_t steering_encoder_get(void)
{
    return (steering_encoder_sample_t){0};
}
const char *wheel_speed_state_name(wheel_speed_state_t state)
{
    (void)state;
    return "WAITING";
}
const char *steering_encoder_state_name(steering_encoder_state_t state)
{
    (void)state;
    return "WAITING";
}
void vehicle_control_log_status(const char *remote, uint8_t mode,
                                const steering_encoder_sample_t *encoder,
                                const wheel_speed_sample_t *right)
{
    assert(strcmp(remote, "OK") == 0 && mode == 1U && encoder != NULL && right != NULL);
    ++s_status_calls;
    (void)vehicle_log_publish(
        &(const vehicle_log_message_t){.kind = VEHICLE_LOG_STATUS,
                                       .data.status = {.remote_state = remote,
                                                       .drive_mode = mode,
                                                       .drive_state = "DISABLED",
                                                       .steering_state = "OFF"}});
}
uint32_t motor_safety_poll(void)
{
    return s_fault;
}
uint32_t motor_safety_faults(void)
{
    return s_fault;
}
motor_fault_snapshot_t motor_safety_first_fault(void)
{
    return (motor_fault_snapshot_t){.first = s_fault, .wake_age_ms = UINT32_MAX};
}
uint32_t control_watchdog_reset_flags(void)
{
    return 0U;
}
_Noreturn void fault_shutdown_halt(void)
{
    assert(false);
    for (;;) {
    }
}
void vehicle_control_update(const remote_command_t *control,
                            const steering_encoder_sample_t *encoder,
                            const wheel_speed_sample_t *right_speed)
{
    assert(encoder != NULL && right_speed != NULL);
    assert(s_feeds == s_app_updates); /* No refresh before control completes. */
    if (s_fault == 0U) {
        assert(control != NULL && control->throttle == 123);
    } else {
        assert(control == NULL); /* FLT blocks an otherwise valid RC command. */
    }
    ++s_app_updates;
}
void control_watchdog_feed(void)
{
    assert(!s_in_isr && s_app_updates == s_feeds + 1U);
    ++s_feeds;
}
TickType_t xTaskGetTickCount(void)
{
    command(30U); /* Arrives after transport init, before the first control pass. */
    return s_now;
}
void vTaskDelayUntil(TickType_t *last_wake, TickType_t increment)
{
    assert(increment == 1U && s_feeds == s_app_updates);
    *last_wake += increment;
    s_now = *last_wake;
    ++s_delays;
    if (s_inject_fault && s_delays == 2U) {
        s_fault = 1U;
    }
    if ((s_delays % 20U) == 0U) {
        command((uint8_t)(30U + s_delays / 20U));
    }
    if (s_delays == s_app_cycles) {
        longjmp(s_app_exit, 1);
    }
}
static void test_application_owns_control_and_watchdog(void)
{
    init();
    s_app_updates = 0U;
    s_feeds = 0U;
    s_delays = 0U;
    s_fault = 0U;
    s_app_cycles = 3U;
    s_inject_fault = true;
    s_status_calls = 0U;
    if (setjmp(s_app_exit) == 0) {
        vehicle_app_run(&s_uart);
    }
    assert(s_app_updates == 3U && s_feeds == 3U && s_delays == 3U);
    assert(s_console_inits == 0U); /* The control task never initializes RTT. */
    assert(strstr(s_log, "MOTOR_FLT LATCHED") != NULL);
}
static void test_compact_status_period_and_tick_wrap(void)
{
    init();
    s_now = UINT32_MAX - 700U;
    s_app_updates = 0U;
    s_feeds = 0U;
    s_delays = 0U;
    s_fault = 0U;
    s_app_cycles = 1201U;
    s_inject_fault = false;
    s_status_calls = 0U;
    if (setjmp(s_app_exit) == 0) {
        vehicle_app_run(&s_uart);
    }
    assert(s_status_calls == 2U && s_feeds == 1201U);
    assert(strstr(s_log, "UART_BAD") == NULL && strstr(s_log, "STEER_ENC") == NULL);
    assert(strstr(s_log, "DRIVE_CTRL") == NULL && strstr(s_log, "REAR_RIGHT") == NULL);
    init();
    s_app_updates = 0U;
    s_feeds = 0U;
    s_delays = 0U;
    s_fault = 0U;
    s_app_cycles = 501U;
    s_inject_fault = true;
    s_status_calls = 0U;
    if (setjmp(s_app_exit) == 0) {
        vehicle_app_run(&s_uart);
    }
    assert(s_status_calls == 0U && strstr(s_log, "MOTOR BLOCKED fault=0x01") != NULL);
}

static void test_full_log_queue_cannot_stop_control_or_watchdog(void)
{
    init();
    s_fail_log = true;
    s_app_updates = 0U;
    s_feeds = 0U;
    s_delays = 0U;
    s_fault = 0U;
    s_app_cycles = 1201U;
    s_inject_fault = false;
    s_status_calls = 0U;
    if (setjmp(s_app_exit) == 0) {
        vehicle_app_run(&s_uart);
    }
    assert(s_app_updates == 1201U && s_feeds == 1201U && s_delays == 1201U);
    assert(s_status_calls == 2U && s_console_inits == 0U && s_log[0] == '\0');
    s_fail_log = false;
}

int main(void)
{
    assert(!remote_uart_init(NULL));
    test_error_irq_diagnostics();
    test_order_and_real_arrival_grace();
    test_overflow_preserves_prefix_and_reports_first_loss();
    test_gap_error_and_rearm_recovery();
    test_application_owns_control_and_watchdog();
    test_compact_status_period_and_tick_wrap();
    test_full_log_queue_cannot_stop_control_or_watchdog();
    return 0;
}
