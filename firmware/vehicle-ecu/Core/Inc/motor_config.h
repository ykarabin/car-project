#ifndef VEHICLE_MOTOR_CONFIG_H
#define VEHICLE_MOTOR_CONFIG_H

/* Per-driver FLT thresholds, sampled by the 1 ms control loop. */
#define MOTOR_FAULT_CONFIRM_MS 2U
#define MOTOR_FAULT_PULSE_WINDOW_MS 1000U
#define MOTOR_FAULT_PULSE_LIMIT 3U
/* Ignore brief SLP-transition edges in the burst count; sustained LOW still qualifies. */
#define MOTOR_FAULT_TRANSITION_MS 2U

/* Invert DIR if needed: rear positive means forward, steering positive means right. */
#ifndef MOTOR_LEFT_DIRECTION_INVERTED
#define MOTOR_LEFT_DIRECTION_INVERTED 1
#endif

#ifndef MOTOR_RIGHT_DIRECTION_INVERTED
#define MOTOR_RIGHT_DIRECTION_INVERTED 1
#endif

#ifndef MOTOR_STEERING_DIRECTION_INVERTED
#define MOTOR_STEERING_DIRECTION_INVERTED 0
#endif

#endif
