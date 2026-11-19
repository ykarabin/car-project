#ifndef VEHICLE_STEERING_CONTROLLER_H
#define VEHICLE_STEERING_CONTROLLER_H

#include "steering_calibration.h"

/* One task owns this context. Diagnostic fields are read-only to callers. */
typedef struct {
    steering_calibration_t calibration;
    int32_t direction;
    int32_t left_counts;
    int32_t right_counts;
    int32_t error_counts;
    uint32_t updated_at_ms;
    uint32_t feedback_at_ms;
    uint16_t target_raw;
    uint16_t reference_raw; /* Actual raw on restart, then the last applied target. */
    int16_t pwm;
    bool calibrated;
    bool initialized;
    bool moving;
} steering_controller_t;

bool steering_controller_init(steering_controller_t *controller,
                              const steering_calibration_t *calibration);
void steering_controller_stop(steering_controller_t *controller);

/* Standard C, fixed-period P position controller, no HAL/RTOS calls.
 * Brief invalid feedback only reduces PWM, up to 40 ms; invalid input and
 * prolonged feedback loss stop. Restart seeds diagnostics from the actual
 * position and returns zero PWM. The next control update uses the full target;
 * only the PWM output is slew-limited. Returns signed motor PWM permille. */
int16_t steering_controller_update(steering_controller_t *controller, int16_t request,
                                   uint16_t raw, bool feedback_valid, uint32_t now_ms);

#endif
