#ifndef VEHICLE_APP_H
#define VEHICLE_APP_H

#include "stm32g4xx_hal.h"

/* Run the control loop after motor initialization; this task also feeds IWDG. */
void vehicle_app_run(UART_HandleTypeDef *uart);

#endif
