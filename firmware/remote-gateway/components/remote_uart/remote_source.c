#include "remote_source.h"

remote_source_t remote_source_select(remote_source_t active, bool elrs_valid, int64_t elrs_since_us,
                                     bool pwm_valid, int64_t pwm_since_us)
{
    if (active == REMOTE_SOURCE_ELRS) {
        return elrs_valid ? REMOTE_SOURCE_ELRS : REMOTE_SOURCE_NONE;
    }
    if (active == REMOTE_SOURCE_PWM) {
        return pwm_valid ? REMOTE_SOURCE_PWM : REMOTE_SOURCE_NONE;
    }
    if (active != REMOTE_SOURCE_NONE) {
        return REMOTE_SOURCE_NONE;
    }
    if (elrs_valid && (!pwm_valid || (elrs_since_us <= pwm_since_us))) {
        return REMOTE_SOURCE_ELRS;
    }
    return pwm_valid ? REMOTE_SOURCE_PWM : REMOTE_SOURCE_NONE;
}
