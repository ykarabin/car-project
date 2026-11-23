#ifndef VEHICLE_CONTROL_H
#define VEHICLE_CONTROL_H

#include "remote_protocol.h"
#include "steering_encoder.h"
#include "wheel_speed.h"

/* Control-task coordinator for drive, steering, calibration and motor outputs. */
void vehicle_control_init(void);
void vehicle_control_update(const remote_command_t *command,
                            const steering_encoder_sample_t *encoder,
                            const wheel_speed_sample_t *right_speed);
/* Queue a status snapshot; remote_state must reference immutable static storage. */
void vehicle_control_log_status(const char *remote_state, uint8_t mode,
                                const steering_encoder_sample_t *encoder,
                                const wheel_speed_sample_t *right);

#endif
