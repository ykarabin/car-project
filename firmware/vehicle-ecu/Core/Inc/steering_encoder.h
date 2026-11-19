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
    uint16_t raw; /* Absolute 0..4095; use only when state == OK. */
    steering_encoder_state_t state;
} steering_encoder_sample_t;

/* Call once after CubeMX configures TIM4 PWM input at 10 MHz on PB6.
 * CH2 captures falling edges without IRQ; CH1 interrupts on rising edges. */
bool steering_encoder_init(TIM_HandleTypeDef *timer);

/* TIM4 IRQ entry: call before HAL_TIM_IRQHandler clears capture flags.
 * Captures raw data only; decoding and logging remain in task context. */
void steering_encoder_irq(void);

/* Task context. Takes a coherent snapshot with a brief IRQ mask; never blocks. */
steering_encoder_sample_t steering_encoder_get(void);
const char *steering_encoder_state_name(steering_encoder_state_t state);

#endif
