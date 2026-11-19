#ifndef VEHICLE_DRIVE_CONTROLLER_CONFIG_H
#define VEHICLE_DRIVE_CONTROLLER_CONFIG_H

#include <stdint.h>

#include "rear_encoder_config.h"
#include "vehicle_input_config.h"

/* Speed in mm/s; floor the finite limits to stay below 4 and 6 km/h. */
#define DRIVE_CONTROL_MODE1_MAX_MM_S 1111
#define DRIVE_CONTROL_MODE2_MAX_MM_S 1666
/* Nominal scale for partial throttle and feed-forward, not a mode-3 cap.
 * Full stick in mode 3 requests 100% PWM, with valid feedback and direction. */
#define DRIVE_CONTROL_NOMINAL_MOTOR_RPM 2800U
#define DRIVE_CONTROL_NOMINAL_MM_S \
    ((int32_t)(((uint64_t)DRIVE_CONTROL_NOMINAL_MOTOR_RPM * \
                 REAR_ENCODER_WHEEL_CIRCUMFERENCE_UM) / \
                ((uint64_t)REAR_ENCODER_GEAR_RATIO * 60000U)))

#define DRIVE_CONTROL_PERIOD_MS 10U
#define DRIVE_CONTROL_MAX_GAP_MS 50U
#define DRIVE_CONTROL_FEEDBACK_MAX_AGE_MS 20U
#define DRIVE_CONTROL_FEEDBACK_GRACE_MS 40U
/* Above the rear-encoder 6000 RPM diagnostic bound (about 4556 mm/s). */
#define DRIVE_CONTROL_FEEDBACK_MAX_MM_S 5000
#define DRIVE_CONTROL_FULL_STICK 990
#define DRIVE_CONTROL_PWM_MAX 1000
/* Ordinary driving: rise/fall 1% per 10 ms (full range 1 s).
 * Deliberate braking/reversal: 2% per 10 ms (full range 0.5 s).
 * Invalid input/feedback still stop immediately. */
#define DRIVE_CONTROL_PWM_RISE_STEP 10
#define DRIVE_CONTROL_PWM_FALL_STEP 10
#define DRIVE_CONTROL_PWM_BRAKE_STEP 20
/* Raised-wheel test: about 30% PWM holds 3 km/h. Use a conservative
 * feed-forward (75% at nominal speed); I supplies the load correction. */
#define DRIVE_CONTROL_FEED_FORWARD_PWM 750
/* Gains in PWM permille per m/s, and PWM permille per (m/s * second). */
#define DRIVE_CONTROL_KP 100
#define DRIVE_CONTROL_KI 300
#define DRIVE_CONTROL_REVERSE_STOP_MM_S 50
#define DRIVE_CONTROL_REVERSE_SETTLE_MS 100U

#endif
