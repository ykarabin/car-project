#ifndef VEHICLE_REAR_ENCODER_CONFIG_H
#define VEHICLE_REAR_ENCODER_CONFIG_H

/* RIGHT encoder is active; LEFT wiring remains reserved. */
#define REAR_ENCODER_RIGHT_COUNTS_PER_MOTOR_REV 4096U
#define REAR_ENCODER_RIGHT_SAMPLE_MS 10U
#define REAR_ENCODER_RIGHT_MAX_GAP_MS 50U
#define REAR_ENCODER_GEAR_RATIO 20U
/* Nominal circumference for a 290 mm wheel. */
#define REAR_ENCODER_WHEEL_CIRCUMFERENCE_UM 911062U
/* Plausibility bound, independent of commanded speed. */
#define REAR_ENCODER_MAX_MOTOR_RPM 6000U

/* Encoder polarity is independent of DIR; positive speed means forward. */
#ifndef REAR_ENCODER_RIGHT_INVERTED
#define REAR_ENCODER_RIGHT_INVERTED 0
#endif

#endif
