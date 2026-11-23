#ifndef TEST_QUEUE_H
#define TEST_QUEUE_H
#include "FreeRTOS.h"
typedef struct {
    uint8_t *storage;
    size_t length, item_size, head, count;
} StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;
QueueHandle_t xQueueCreateStatic(UBaseType_t length, UBaseType_t size,
                                 uint8_t *storage, StaticQueue_t *queue);
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *wake);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
#endif
