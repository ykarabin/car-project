#ifndef VEHICLE_MOTOR_CONFIG_H
#define VEHICLE_MOTOR_CONFIG_H

/* Set each flag to 1 if that motor turns opposite to the commanded direction.
 * Rear positive = vehicle forward; steering positive = turn right.
 * Physical polarity must be checked on the bench. */
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
