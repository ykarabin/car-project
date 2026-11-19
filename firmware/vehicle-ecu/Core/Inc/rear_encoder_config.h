#ifndef VEHICLE_REAR_ENCODER_CONFIG_H
#define VEHICLE_REAR_ENCODER_CONFIG_H

/* Active RIGHT sensor only. LEFT application support is deferred until its
 * physical reconfiguration; CubeMX wiring remains reserved. */
#define REAR_ENCODER_RIGHT_COUNTS_PER_MOTOR_REV 4096U
#define REAR_ENCODER_RIGHT_SAMPLE_MS 10U
#define REAR_ENCODER_RIGHT_MAX_GAP_MS 50U
#define REAR_ENCODER_GEAR_RATIO 20U
/* pi * 290 mm, rounded to micrometres. Rolling circumference is not measured. */
#define REAR_ENCODER_WHEEL_CIRCUMFERENCE_UM 911062U
/* Wide diagnostic plausibility bound; not a motor speed command/limit. */
#define REAR_ENCODER_MAX_MOTOR_RPM 6000U

/* Independent from motor DIR inversion. Positive speed must mean vehicle
 * forward; verify the RIGHT sign with physical wheel rotation on the bench. */
#ifndef REAR_ENCODER_RIGHT_INVERTED
#define REAR_ENCODER_RIGHT_INVERTED 0
#endif

#endif
