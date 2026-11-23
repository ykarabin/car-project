#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
#include <stddef.h>
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef struct { uint8_t bytes[92]; } StaticTask_t;
#define tskIDLE_PRIORITY 0U
#define configMAX_PRIORITIES 56U
#define portBYTE_ALIGNMENT 8U
#define portMAX_DELAY UINT32_MAX
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portYIELD_FROM_ISR(wake) ((void)(wake))
#endif
