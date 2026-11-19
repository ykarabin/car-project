#ifndef VEHICLE_REAR_ENCODERS_H
#define VEHICLE_REAR_ENCODERS_H

#include "stm32g4xx_hal.h"
#include "wheel_speed.h"

/* RIGHT encoder only. LEFT CubeMX pins/TIM2 remain reserved, not started.
 * Call after CubeMX TIM3/GPIO setup, before starting the RTOS. */
bool rear_encoders_init(TIM_HandleTypeDef *right);
/* One task owns periodic speed updates and reads. No A/B or Z interrupts. */
void rear_encoders_update(void);
wheel_speed_sample_t rear_encoders_get(void);

#endif
