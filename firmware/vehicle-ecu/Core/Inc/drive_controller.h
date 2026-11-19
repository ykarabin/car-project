#ifndef VEHICLE_DRIVE_CONTROLLER_H
#define VEHICLE_DRIVE_CONTROLLER_H

#include "remote_protocol.h"
#include "wheel_speed.h"

typedef enum {
    DRIVE_CONTROL_DISABLED,
    DRIVE_CONTROL_WAITING,
    DRIVE_CONTROL_STOP,
    DRIVE_CONTROL_STOPPING,
    DRIVE_CONTROL_TRACKING,
    DRIVE_CONTROL_FULL,
    DRIVE_CONTROL_BRAKING,
    DRIVE_CONTROL_REVERSING,
    DRIVE_CONTROL_FEEDBACK_HOLD,
    DRIVE_CONTROL_FEEDBACK_ERROR
} drive_control_state_t;

typedef enum {
    DRIVE_REVERSE_BRAKE_REQUIRED = 0,
    DRIVE_REVERSE_WAIT_NEUTRAL,
    DRIVE_REVERSE_READY,
    DRIVE_REVERSE_ACTIVE,
    /* A settled neutral stop after forward drive can also permit reverse. */
    DRIVE_REVERSE_FORWARD_SEEN
} drive_reverse_stage_t;

typedef enum {
    DRIVE_MOTION_IDLE = 0,
    DRIVE_MOTION_RUNNING,
    DRIVE_MOTION_REVERSING,
    DRIVE_MOTION_REVERSE_SETTLE,
    DRIVE_MOTION_NEUTRAL_SETTLE
} drive_motion_phase_t;

/* Shared rear controller using RIGHT feedback; owned by the control task. */
typedef struct {
    int32_t target_mm_s;
    int32_t measured_mm_s;
    int32_t error_mm_s;
    int32_t integral_milli_pwm;
    int32_t direction;
    uint32_t updated_at_ms;
    uint32_t stopped_since_ms;
    uint32_t feedback_at_ms;
    int16_t pwm;
    drive_control_state_t state; /* Last diagnostic result. */
    drive_reverse_stage_t reverse_stage;
    drive_motion_phase_t motion;
    bool full_power_requested;
    bool feedback_seen; /* Valid feedback since the last stop or prolonged fault. */
} drive_controller_t;

void drive_controller_stop(drive_controller_t *controller);
/* Returns signed PWM for both rear motors. Reverse requires braking and a settled stop.
 * Brief feedback loss only reduces output; invalid commands or prolonged faults stop it. */
int16_t drive_controller_update(drive_controller_t *controller,
                                const remote_command_t *command,
                                const wheel_speed_sample_t *right, uint32_t now_ms);
const char *drive_controller_state_name(drive_control_state_t state);

#endif
