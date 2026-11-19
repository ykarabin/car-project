#ifndef VEHICLE_APP_H
#define VEHICLE_APP_H

#include "stm32g4xx_hal.h"

/* Single default task owns controllers, calibration, motor outputs and IWDG.
 * motor_control_init() must succeed before starting this task. */
void vehicle_app_run(UART_HandleTypeDef *uart);

#endif
