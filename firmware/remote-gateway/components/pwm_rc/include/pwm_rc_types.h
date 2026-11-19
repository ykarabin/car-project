#ifndef REMOTE_GATEWAY_PWM_RC_TYPES_H
#define REMOTE_GATEWAY_PWM_RC_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define PWM_RC_CHANNEL_COUNT 3U

typedef struct {
    bool valid;
    int64_t received_at_us; /* Oldest required channel, esp_timer time base. */
    int64_t valid_since_us;
    int64_t channel_received_at_us[PWM_RC_CHANNEL_COUNT];
    int16_t normalized[PWM_RC_CHANNEL_COUNT];
    uint32_t pulse_us[PWM_RC_CHANNEL_COUNT]; /* Diagnostics, also when invalid. */
    uint32_t period_us[PWM_RC_CHANNEL_COUNT];
    uint32_t pulses;
    uint32_t errors;
} pwm_rc_sample_t;

#endif
