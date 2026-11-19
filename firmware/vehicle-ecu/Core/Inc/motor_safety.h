#ifndef VEHICLE_MOTOR_SAFETY_H
#define VEHICLE_MOTOR_SAFETY_H

#include <stdbool.h>
#include <stdint.h>

#define MOTOR_FAULT_LEFT UINT32_C(1)
#define MOTOR_FAULT_RIGHT UINT32_C(2)
#define MOTOR_FAULT_STEERING UINT32_C(4)

/* Boot/fatal path: enable GPIOD, preload SLP LOW and configure PD8/9/10
 * push-pull outputs. Independent of HAL handles, tick and initialized RAM.
 * External pull-downs still determine SLP before firmware starts and at reset. */
void motor_safety_early_off(void);

/* EXTI callback dispatches here even before encoder initialization.
 * Any driver FLT latches a global motor inhibit until MCU reset. */
void motor_safety_fault_irq(uint16_t pin);
/* Task context: also catches an already LOW input or pending short pulse. */
uint32_t motor_safety_poll(void);
uint32_t motor_safety_faults(void);

/* Sole path for raising SLP: checks the latch, FLT levels and pending EXTI
 * with IRQs masked; rechecks after the write. False leaves all drivers asleep. */
bool motor_safety_wake(bool rear_enabled, bool steering_enabled);

#endif
