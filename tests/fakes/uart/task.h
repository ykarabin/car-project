#ifndef TEST_TASK_H
#define TEST_TASK_H
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
typedef StaticTask_t *TaskHandle_t;
TaskHandle_t xTaskCreateStatic(TaskFunction_t entry, const char *name, uint32_t words,
                              void *argument, UBaseType_t priority, StackType_t *stack,
                              StaticTask_t *control);
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t *last_wake, TickType_t increment);
#endif
