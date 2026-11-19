#ifndef VEHICLE_DRIVE_CONTROLLER_CONFIG_H
#define VEHICLE_DRIVE_CONTROLLER_CONFIG_H

#include <stdint.h>

#include "rear_encoder_config.h"
#include "vehicle_input_config.h"

/* Speed limits in mm/s, rounded down from 4 and 6 km/h. */
#define DRIVE_CONTROL_MODE1_MAX_MM_S 1111
#define DRIVE_CONTROL_MODE2_MAX_MM_S 1666
/* Scale for partial throttle and feed-forward; full mode-3 throttle requests 100% PWM. */
#define DRIVE_CONTROL_NOMINAL_MOTOR_RPM 2800U
#define DRIVE_CONTROL_NOMINAL_MM_S \
    ((int32_t)(((uint64_t)DRIVE_CONTROL_NOMINAL_MOTOR_RPM * \
                 REAR_ENCODER_WHEEL_CIRCUMFERENCE_UM) / \
                ((uint64_t)REAR_ENCODER_GEAR_RATIO * 60000U)))

#define DRIVE_CONTROL_PERIOD_MS 10U
#define DRIVE_CONTROL_MAX_GAP_MS 50U
#define DRIVE_CONTROL_FEEDBACK_MAX_AGE_MS 20U
#define DRIVE_CONTROL_FEEDBACK_GRACE_MS 40U
/* Plausibility bound above the encoder limit. */
#define DRIVE_CONTROL_FEEDBACK_MAX_MM_S 5000
#define DRIVE_CONTROL_FULL_STICK 990
#define DRIVE_CONTROL_PWM_MAX 1000
/* PWM increments per control period; braking uses a faster decrease. */
#define DRIVE_CONTROL_PWM_RISE_STEP 10
#define DRIVE_CONTROL_PWM_FALL_STEP 10
#define DRIVE_CONTROL_PWM_BRAKE_STEP 20
/* Feed-forward PWM at nominal speed, in permille. */
#define DRIVE_CONTROL_FEED_FORWARD_PWM 750
/* PI gains: PWM permille per m/s and per (m/s * second). */
#define DRIVE_CONTROL_KP 100
#define DRIVE_CONTROL_KI 300
#define DRIVE_CONTROL_REVERSE_STOP_MM_S 50
#define DRIVE_CONTROL_REVERSE_SETTLE_MS 100U

#endif
