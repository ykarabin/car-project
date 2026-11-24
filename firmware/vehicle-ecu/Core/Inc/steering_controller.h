#ifndef VEHICLE_STEERING_CONTROLLER_H
#define VEHICLE_STEERING_CONTROLLER_H

#include "steering_calibration.h"

/* Control-task context; diagnostic fields are read-only. */
typedef struct {
    steering_calibration_t calibration;
    int32_t direction;
    int32_t left_counts;
    int32_t right_counts;
    int32_t error_counts;
    int32_t stall_reference_counts;
    uint32_t updated_at_ms;
    uint32_t feedback_at_ms;
    uint32_t stall_since_ms;
    uint16_t target_raw;
    int16_t pwm;
    bool calibrated;
    bool initialized;
    bool moving;
    bool stalled; /* latched until steering_controller_stop() */
} steering_controller_t;

/* Build working travel limits from the saved calibration. */
bool steering_controller_init(steering_controller_t *controller,
                              const steering_calibration_t *calibration);
void steering_controller_stop(steering_controller_t *controller);

/* P position controller returning signed PWM in +/-1000.
 * Brief feedback loss reduces output; restart begins at zero PWM.
 * Driving without position progress for STEERING_CONTROL_STALL_TIMEOUT_MS sets `stalled`
 * and returns 0 until steering_controller_stop(). */
int16_t steering_controller_update(steering_controller_t *controller, int16_t request, uint16_t raw,
                                   bool feedback_valid, uint32_t now_ms);

#endif
