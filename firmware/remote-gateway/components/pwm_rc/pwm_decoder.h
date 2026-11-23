#ifndef REMOTE_GATEWAY_PWM_DECODER_H
#define REMOTE_GATEWAY_PWM_DECODER_H

#include <stddef.h>

#include "pwm_rc_types.h"

#define PWM_RC_MIN_PULSE_US 800U
#define PWM_RC_MAX_PULSE_US 2200U
#define PWM_RC_MIN_PERIOD_US 3000U
#define PWM_RC_MAX_PERIOD_US 50000U
#define PWM_RC_CONFIRM_PULSES 3U

typedef struct {
    uint16_t minimum_us;
    uint16_t center_us;
    uint16_t maximum_us;
    uint16_t deadband_us;
    bool inverted;
} pwm_calibration_t;

typedef struct {
    uint32_t rise_ticks;
    int64_t rise_at_us;
    bool have_rise;
    bool rise_pending;
    bool period_valid;
    uint8_t good_pulses;
    bool valid;
    uint32_t pulse_us;
    uint32_t period_us;
    int64_t received_at_us;
    int64_t valid_since_us;
    uint32_t pulses;
    uint32_t errors;
} pwm_channel_t;

typedef struct {
    uint32_t ticks_per_us; /* Capture clock must be an integer MHz. */
    pwm_channel_t channels[PWM_RC_CHANNEL_COUNT];
} pwm_decoder_t;

bool pwm_calibration_valid(const pwm_calibration_t *calibration);

/* Zero-initialize and set ticks_per_us; serialize capture and snapshot access. */
void pwm_decoder_capture(pwm_decoder_t *decoder, size_t channel, bool rising,
                         uint32_t capture_ticks, int64_t now_us);
void pwm_decoder_sample(const pwm_decoder_t *decoder,
                        const pwm_calibration_t calibration[PWM_RC_CHANNEL_COUNT], int64_t now_us,
                        int64_t timeout_us, pwm_rc_sample_t *sample);

#endif
