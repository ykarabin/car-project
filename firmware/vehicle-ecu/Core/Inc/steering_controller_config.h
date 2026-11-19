#ifndef VEHICLE_STEERING_CONTROLLER_CONFIG_H
#define VEHICLE_STEERING_CONTROLLER_CONFIG_H

#include "vehicle_input_config.h"

/* Responsive position tuning. PWM units are permille; encoder units are
 * 1/4096 turn. The full target is applied on the next control update.
 * Output reaches the 70% ceiling within 40 ms; service jog remains at 15%. */
#define STEERING_CONTROL_PERIOD_MS 10U
#define STEERING_CONTROL_MAX_GAP_MS 50U
#define STEERING_CONTROL_FEEDBACK_GRACE_MS 40U
#define STEERING_CONTROL_PWM_STEP 200
#define STEERING_CONTROL_KP 8
#define STEERING_CONTROL_PWM_MAX 700
#define STEERING_CONTROL_PWM_MIN 120
#define STEERING_CONTROL_STOP_COUNTS 4
#define STEERING_CONTROL_START_COUNTS 8
/* On stick release, return to center before allowing manual steering.
 * End after a stable center, or release on timeout without retrying. */
#define STEERING_CONTROL_RELEASE_SETTLE_MS 100U
#define STEERING_CONTROL_RELEASE_TIMEOUT_MS 2000U

#endif
