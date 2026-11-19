#ifndef VEHICLE_CONTROL_H
#define VEHICLE_CONTROL_H

#include "remote_protocol.h"
#include "steering_encoder.h"
#include "wheel_speed.h"

/* Single default task owns rear drive, steering, calibration and motor outputs.
 * Calibration uses the board button; RTT is diagnostic output only. */
void vehicle_control_init(void);
void vehicle_control_update(const remote_command_t *command,
                             const steering_encoder_sample_t *encoder,
                             const wheel_speed_sample_t *right_speed);
/* Copy a diagnostic snapshot to the logger, called by the owner at 500 ms.
 * remote_state must be an immutable static state-name string. No formatting. */
void vehicle_control_log_status(const char *remote_state, uint8_t mode,
                                 const steering_encoder_sample_t *encoder,
                                 const wheel_speed_sample_t *right);

#endif
