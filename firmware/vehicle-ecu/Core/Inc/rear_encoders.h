#ifndef VEHICLE_REAR_ENCODERS_H
#define VEHICLE_REAR_ENCODERS_H

#include "stm32g4xx_hal.h"
#include "wheel_speed.h"

/* Start the RIGHT encoder after TIM3/GPIO setup, before the scheduler. */
bool rear_encoders_init(TIM_HandleTypeDef *right);
/* Update and read speed from the control task. */
void rear_encoders_update(void);
wheel_speed_sample_t rear_encoders_get(void);

#endif
