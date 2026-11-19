#include "vehicle_log.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "debug_console.h"
#include "stm32g4xx_hal.h"

#define LOG_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)

typedef struct {
    uint32_t captured_at_ms;
    vehicle_log_message_t message;
} log_record_t;

static QueueHandle_t s_log_queue;
static StaticQueue_t s_log_queue_control;
static uint8_t s_log_queue_storage[VEHICLE_LOG_QUEUE_LENGTH * sizeof(log_record_t)];
static StaticTask_t s_log_task_control;
_Alignas(portBYTE_ALIGNMENT) static StackType_t s_log_task_stack[VEHICLE_LOG_STACK_WORDS];

_Static_assert(LOG_TASK_PRIORITY < configMAX_PRIORITIES, "Logger priority range");

static void log_task(void *argument)
{
    (void)argument;
    debug_console_init();
    for (;;) {
        log_record_t record;
        if (xQueueReceive(s_log_queue, &record, portMAX_DELAY) == pdTRUE) {
            vehicle_log_format(record.captured_at_ms, &record.message);
        }
    }
}

bool vehicle_log_start(void)
{
    s_log_queue = xQueueCreateStatic(VEHICLE_LOG_QUEUE_LENGTH, sizeof(log_record_t),
                                 s_log_queue_storage, &s_log_queue_control);
    if (s_log_queue == NULL) {
        return false;
    }
    if (xTaskCreateStatic(log_task, "rttLog", VEHICLE_LOG_STACK_WORDS, NULL,
                          LOG_TASK_PRIORITY, s_log_task_stack, &s_log_task_control) == NULL) {
        s_log_queue = NULL;
        return false;
    }
    return true;
}

bool vehicle_log_publish(const vehicle_log_message_t *message)
{
    if ((message == NULL) || (s_log_queue == NULL)) {
        return false;
    }
    const log_record_t record = {.captured_at_ms = HAL_GetTick(), .message = *message};
    return xQueueSend(s_log_queue, &record, 0U) == pdTRUE;
}

void vehicle_log_text(const char *static_text)
{
    const vehicle_log_message_t message = {.kind = VEHICLE_LOG_TEXT, .data.text = static_text};
    (void)vehicle_log_publish(&message);
}
