#ifndef VEHICLE_STEERING_ENCODER_H
#define VEHICLE_STEERING_ENCODER_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32g4xx_hal.h"

#define STEERING_ENCODER_TIMEOUT_MS UINT32_C(10)

typedef enum {
    STEERING_ENCODER_WAITING,
    STEERING_ENCODER_OK,
    STEERING_ENCODER_INVALID,
    STEERING_ENCODER_STALE
} steering_encoder_state_t;

typedef struct {
    uint16_t raw; /* Absolute 0..4095; valid only in state OK. */
    steering_encoder_state_t state;
} steering_encoder_sample_t;

/* Start TIM4 PWM capture at 10 MHz; rising edges trigger the ISR. */
bool steering_encoder_init(TIM_HandleTypeDef *timer);

/* Call before HAL_TIM_IRQHandler clears capture flags; decode later in task context. */
void steering_encoder_irq(void);

/* Copy a coherent sample with a brief IRQ mask. */
steering_encoder_sample_t steering_encoder_get(void);
const char *steering_encoder_state_name(steering_encoder_state_t state);

#endif
