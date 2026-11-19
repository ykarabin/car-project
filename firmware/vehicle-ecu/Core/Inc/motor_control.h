#ifndef VEHICLE_MOTOR_CONTROL_H
#define VEHICLE_MOTOR_CONTROL_H

#include <stdbool.h>

#include <stdint.h>
#include "stm32g4xx_hal.h"

/* Call once after GPIO and TIM1 PWM initialization, before starting the RTOS.
 * Starts all three PWM channels at zero, with all drivers asleep.
 * Configures TIM1 debug freeze and driven LOW idle outputs (OSSI=1/OIS=0). */
bool motor_control_init(TIM_HandleTypeDef *pwm_timer);

/* Signed PWM in +/-1000, common rear output and independent bridge enables.
 * Zero-initialization means coast. An enabled bridge at PWM=0 brakes.
 * These are actuator requests, independent of UART status/mode/sequence. */
typedef struct {
    int16_t rear_pwm;
    int16_t steering_pwm;
    bool rear_enabled;
    bool steering_enabled;
} motor_request_t;

/* Single task owns outputs. NULL or out-of-range PWM disables all drivers.
 * Disabled outputs ignore their PWM; steering enable is explicit even at zero.
 * FLT inhibits every enable until reset. Caller owns ramps, remote validation
 * and service limits (rear disabled, steering at most +/-150). */
void motor_control_apply(const motor_request_t *request);

#endif
