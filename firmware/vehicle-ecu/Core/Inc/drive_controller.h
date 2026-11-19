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
    /* First reverse pull still brakes, but a settled neutral stop can unlock. */
    DRIVE_REVERSE_FORWARD_SEEN
} drive_reverse_stage_t;

typedef enum {
    DRIVE_MOTION_IDLE = 0,        /* No established control timing. */
    DRIVE_MOTION_RUNNING,         /* Timing established, including RC braking. */
    DRIVE_MOTION_REVERSING,       /* Ramp to zero / wait for wheel stop. */
    DRIVE_MOTION_REVERSE_SETTLE,  /* Timed stop before changing direction. */
    DRIVE_MOTION_NEUTRAL_SETTLE   /* Timed neutral stop after forward drive. */
} drive_motion_phase_t;

/* One task owns this common rear controller. Feedback is RIGHT only.
 * Diagnostic fields are read-only to callers; no HAL/RTOS dependencies. */
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
    uint8_t drive_mode;
    drive_control_state_t state; /* Last result, also records feedback history. */
    drive_reverse_stage_t reverse_stage;
    drive_motion_phase_t motion;
    /* Request metadata, independent of motion/permission. Not a latched phase. */
    bool full_power_requested;
} drive_controller_t;

void drive_controller_stop(drive_controller_t *controller);
/* Returns one signed PWM command for both rear bridges. Ordinary commands,
 * neutral and mode changes obey rise/fall limits. First reverse pull brakes
 * with its faster ramp; neutral and a second pull permit reverse after a settled stop.
 * A settled neutral stop after forward drive also permits the next reverse pull.
 * Startup and faults still require the brake/neutral/reverse sequence.
 * Unusable feedback permits only PWM reduction for up to 40 ms; prolonged
 * failure, invalid command or timing gap stops immediately. Remote fault
 * grace belongs to the command monitor, explicit stop to the caller.
 * Caller retains remote timeout, bridge enable and calibration ownership. */
int16_t drive_controller_update(drive_controller_t *controller,
                                const remote_command_t *command,
                                const wheel_speed_sample_t *right, uint32_t now_ms);
const char *drive_controller_state_name(drive_control_state_t state);

#endif
