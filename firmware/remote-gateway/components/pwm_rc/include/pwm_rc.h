#ifndef REMOTE_GATEWAY_PWM_RC_H
#define REMOTE_GATEWAY_PWM_RC_H

#include "esp_err.h"
#include "pwm_rc_types.h"

/* Owns one MCPWM capture timer and three input channels. Call once at startup. */
esp_err_t pwm_rc_start(void);

/* Thread-safe snapshot. All three channels must be valid and fresh.
 * Invalid snapshots have zeroed normalized channels and timestamps.
 * PWM carries no RF-link flag. Valid failsafe/held pulses remain valid inputs.
 */
esp_err_t pwm_rc_get_sample(pwm_rc_sample_t *sample);

#endif
