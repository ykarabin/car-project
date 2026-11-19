#ifndef VEHICLE_INPUT_CONFIG_H
#define VEHICLE_INPUT_CONFIG_H

#include <stdint.h>

/* Vehicle-side neutral band for both sticks, in +/-1000 command units.
 * Normal control, calibration entry/capture and resume use one policy. */
#define VEHICLE_STICK_DEADBAND INT32_C(30)

#endif
