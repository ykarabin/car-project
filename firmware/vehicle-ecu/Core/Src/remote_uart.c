#include "remote_uart.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "vehicle_log.h"

static UART_HandleTypeDef *s_uart;
static uint8_t s_byte;
static uint32_t s_last_byte_ms;
static remote_parser_t s_parser;
static QueueHandle_t s_queue;
static StaticQueue_t s_queue_control;
static uint8_t s_queue_storage[REMOTE_UART_QUEUE_LENGTH * sizeof(remote_uart_event_t)];

typedef vehicle_log_uart_t uart_rx_diagnostics_t;

static volatile uart_rx_diagnostics_t s_rx_diagnostics;
static uint32_t s_logged_events;
static volatile bool s_overflow;
static volatile uint32_t s_overflow_at_ms;

void remote_uart_capture_irq(UART_HandleTypeDef *uart)
{
    if (uart != s_uart) {
        return;
    }
    const uint32_t flags =
        uart->Instance->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE);
    if (flags != 0U) {
        ++s_rx_diagnostics.events;
        if ((flags & USART_ISR_ORE) != 0U) {
            ++s_rx_diagnostics.overrun;
        }
        if ((flags & USART_ISR_FE) != 0U) {
            ++s_rx_diagnostics.framing;
        }
        if ((flags & USART_ISR_NE) != 0U) {
            ++s_rx_diagnostics.noise;
        }
        if ((flags & USART_ISR_PE) != 0U) {
            ++s_rx_diagnostics.parity;
        }
    }
}

void remote_uart_log_errors(void)
{
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    const uart_rx_diagnostics_t sample = s_rx_diagnostics;
    __set_PRIMASK(saved_mask);
    if (sample.events == s_logged_events) {
        return;
    }
    s_logged_events = sample.events;
    (void)vehicle_log_publish(
        &(const vehicle_log_message_t){.kind = VEHICLE_LOG_UART, .data.uart = sample});
}

static void publish_from_isr(const remote_uart_event_t *received)
{
    BaseType_t wake = pdFALSE;
    /* After overflow, reject new events until the queued prefix is consumed. */
    if (s_overflow || (xQueueSendFromISR(s_queue, received, &wake) != pdTRUE)) {
        if (!s_overflow) {
            s_overflow_at_ms = received->received_at_ms;
        }
        s_overflow = true;
        ++s_rx_diagnostics.events;
        ++s_rx_diagnostics.dropped;
    }
    portYIELD_FROM_ISR(wake);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart != s_uart) {
        return;
    }

    const uint8_t byte = s_byte;
    const HAL_StatusTypeDef armed = HAL_UART_Receive_IT(uart, &s_byte, 1U);
    remote_uart_event_t received = {.received_at_ms = HAL_GetTick()};
    if ((s_parser.used != 0U || s_parser.discarding) &&
        (received.received_at_ms - s_last_byte_ms >= REMOTE_SEND_PERIOD_MS)) {
        /* Discard partial lines after a long gap so stale commands cannot appear fresh. */
        ++s_rx_diagnostics.events;
        ++s_rx_diagnostics.partial_gap;
        remote_parser_reset(&s_parser);
        publish_from_isr(&received);
    }
    s_last_byte_ms = received.received_at_ms;
    const remote_parse_result_t result = remote_parser_push(&s_parser, byte, &received.command);
    if (result == REMOTE_PARSE_INVALID) {
        ++s_rx_diagnostics.events;
        ++s_rx_diagnostics.parse_errors;
    }
    if (armed != HAL_OK) {
        ++s_rx_diagnostics.events;
        ++s_rx_diagnostics.rearm_fail;
        remote_parser_reset(&s_parser);
        publish_from_isr(&received);
    } else if (result != REMOTE_PARSE_NONE) {
        received.valid = (result == REMOTE_PARSE_COMMAND);
        publish_from_isr(&received);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart != s_uart) {
        return;
    }

    ++s_rx_diagnostics.events;
    ++s_rx_diagnostics.hal_errors;
    /* Discard partial input after a UART error. */
    (void)HAL_UART_AbortReceive(uart);
    remote_parser_reset(&s_parser);
    const remote_uart_event_t invalid = {.received_at_ms = HAL_GetTick()};
    publish_from_isr(&invalid);
    if (HAL_UART_Receive_IT(uart, &s_byte, 1U) != HAL_OK) {
        ++s_rx_diagnostics.events;
        ++s_rx_diagnostics.rearm_fail;
    }
}

bool remote_uart_init(UART_HandleTypeDef *uart)
{
    if (uart == NULL) {
        return false;
    }
    remote_parser_reset(&s_parser);
    s_rx_diagnostics = (uart_rx_diagnostics_t){0};
    s_logged_events = 0U;
    s_overflow = false;
    s_last_byte_ms = 0U;
    s_queue = xQueueCreateStatic(REMOTE_UART_QUEUE_LENGTH, sizeof(remote_uart_event_t),
                                 s_queue_storage, &s_queue_control);
    if (s_queue == NULL) {
        return false;
    }
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    s_uart = uart;
    const HAL_StatusTypeDef aborted = HAL_UART_AbortReceive(uart);
    __set_PRIMASK(saved_mask);
    if (aborted != HAL_OK) {
        return false;
    }
    remote_uart_service();
    return true;
}

void remote_uart_service(void)
{
    /* Recover stopped RX with IRQs masked to avoid racing the UART ISR. */
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    if ((s_uart != NULL) && (s_uart->RxState == HAL_UART_STATE_READY)) {
        if (HAL_UART_Receive_IT(s_uart, &s_byte, 1U) != HAL_OK) {
            ++s_rx_diagnostics.events;
            ++s_rx_diagnostics.rearm_fail;
        }
    }
    __set_PRIMASK(saved_mask);
}

bool remote_uart_receive(remote_uart_event_t *event)
{
    if ((event == NULL) || (s_queue == NULL)) {
        return false;
    }
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    bool available = xQueueReceive(s_queue, event, 0U) == pdTRUE;
    if (!available && s_overflow) {
        *event = (remote_uart_event_t){.received_at_ms = s_overflow_at_ms};
        s_overflow = false;
        available = true;
    }
    __set_PRIMASK(saved_mask);
    return available;
}
