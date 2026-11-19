#ifndef REMOTE_GATEWAY_PWM_RC_H
#define REMOTE_GATEWAY_PWM_RC_H

#include "esp_err.h"
#include "pwm_rc_types.h"

/* Start capture for the three PWM inputs once at startup. */
esp_err_t pwm_rc_start(void);

/* Thread-safe snapshot; all channels must be fresh and valid.
 * Valid hold/failsafe pulses cannot be distinguished from live RC input. */
esp_err_t pwm_rc_get_sample(pwm_rc_sample_t *sample);

#endif
