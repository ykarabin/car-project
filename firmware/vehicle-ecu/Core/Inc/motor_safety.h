#ifndef VEHICLE_MOTOR_SAFETY_H
#define VEHICLE_MOTOR_SAFETY_H

#include <stdbool.h>
#include <stdint.h>

#define MOTOR_FAULT_LEFT UINT32_C(1)
#define MOTOR_FAULT_RIGHT UINT32_C(2)
#define MOTOR_FAULT_STEERING UINT32_C(4)

/* First confirmed fault; masks: LEFT=1, RIGHT=2, STEERING=4.
 * SLP is the output latch; wake_age_ms=UINT32_MAX means never awakened. */
typedef struct {
    uint32_t first;
    uint32_t slp_high;
    uint32_t flt_low;
    uint32_t pending;
    uint32_t at_ms;
    uint32_t wake_age_ms;
    uint32_t wake_mask;
} motor_fault_snapshot_t;

/* Force SLP LOW on boot or fatal error, without relying on initialized RAM or HAL. */
void motor_safety_early_off(void);

/* Qualify FLT edges; a confirmed fault inhibits all motors until reset. */
void motor_safety_fault_irq(uint16_t pin);
/* Poll every 1 ms to qualify LOW levels and pending edges. */
uint32_t motor_safety_poll(void);
uint32_t motor_safety_faults(void);
motor_fault_snapshot_t motor_safety_first_fault(void);

/* Only wake path: raw FLT LOW blocks wake; latched faults require reset. */
bool motor_safety_wake(bool rear_enabled, bool steering_enabled);

#endif
