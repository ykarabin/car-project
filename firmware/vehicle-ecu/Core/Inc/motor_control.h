#ifndef VEHICLE_MOTOR_CONTROL_H
#define VEHICLE_MOTOR_CONTROL_H

#include <stdbool.h>

#include <stdint.h>
#include "stm32g4xx_hal.h"

/* Start TIM1 PWM at zero with all drivers asleep; call before the scheduler. */
bool motor_control_init(TIM_HandleTypeDef *pwm_timer);

/* Signed PWM in +/-1000. Enabled at zero means brake; disabled means coast. */
typedef struct {
    int16_t rear_pwm;
    int16_t steering_pwm;
    bool rear_enabled;
    bool steering_enabled;
} motor_request_t;

/* Control-task output only. NULL or invalid PWM disables all drivers.
 * The caller owns ramps and limits; latched FLT blocks wake until reset. */
void motor_control_apply(const motor_request_t *request);

#endif
