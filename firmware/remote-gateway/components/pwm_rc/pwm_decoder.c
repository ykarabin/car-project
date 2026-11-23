#include "pwm_decoder.h"

static void invalidate_channel(pwm_channel_t *channel)
{
    channel->valid = false;
    channel->good_pulses = 0U;
    channel->valid_since_us = 0;
}

bool pwm_calibration_valid(const pwm_calibration_t *calibration)
{
    return (calibration->minimum_us >= PWM_RC_MIN_PULSE_US) &&
           (calibration->maximum_us <= PWM_RC_MAX_PULSE_US) &&
           (calibration->minimum_us < calibration->center_us) &&
           (calibration->center_us < calibration->maximum_us) &&
           (((uint32_t)calibration->center_us - calibration->minimum_us) >
            calibration->deadband_us) &&
           (((uint32_t)calibration->maximum_us - calibration->center_us) >
            calibration->deadband_us);
}

void pwm_decoder_capture(pwm_decoder_t *decoder, size_t index, bool rising, uint32_t capture_ticks,
                         int64_t now_us)
{
    if ((index >= PWM_RC_CHANNEL_COUNT) || (decoder->ticks_per_us == 0U)) {
        return;
    }
    pwm_channel_t *const channel = &decoder->channels[index];

    if (rising) {
        /* Unsigned subtraction handles capture-counter wrap. */
        channel->period_us = (capture_ticks - channel->rise_ticks) / decoder->ticks_per_us;
        channel->period_valid = channel->have_rise && !channel->rise_pending &&
                                (now_us >= channel->rise_at_us) &&
                                ((now_us - channel->rise_at_us) <= (int64_t)PWM_RC_MAX_PERIOD_US) &&
                                (channel->period_us >= PWM_RC_MIN_PERIOD_US) &&
                                (channel->period_us <= PWM_RC_MAX_PERIOD_US);
        if (!channel->period_valid) {
            invalidate_channel(channel);
            if (channel->have_rise) {
                ++channel->errors;
            }
        }
        channel->rise_ticks = capture_ticks;
        channel->rise_at_us = now_us;
        channel->have_rise = true;
        channel->rise_pending = true;
        return;
    }

    if (!channel->rise_pending) {
        /* Ignore an incomplete first pulse. */
        if (channel->have_rise) {
            invalidate_channel(channel);
            ++channel->errors;
        }
        return;
    }
    channel->rise_pending = false;
    channel->pulse_us = (capture_ticks - channel->rise_ticks) / decoder->ticks_per_us;

    if ((now_us < channel->rise_at_us) ||
        ((now_us - channel->rise_at_us) > (int64_t)PWM_RC_MAX_PERIOD_US) ||
        (channel->pulse_us < PWM_RC_MIN_PULSE_US) || (channel->pulse_us > PWM_RC_MAX_PULSE_US)) {
        invalidate_channel(channel);
        ++channel->errors;
        return;
    }
    if (!channel->period_valid) {
        return;
    }

    ++channel->pulses;
    channel->received_at_us = now_us;
    if (channel->good_pulses < PWM_RC_CONFIRM_PULSES) {
        ++channel->good_pulses;
    }
    if (channel->good_pulses >= PWM_RC_CONFIRM_PULSES) {
        if (!channel->valid) {
            channel->valid_since_us = now_us;
        }
        channel->valid = true;
    }
}

/* Scale each side of neutral separately, then apply inversion. */
static int16_t normalize(uint32_t pulse_us, const pwm_calibration_t *calibration)
{
    const int32_t pulse = (int32_t)pulse_us;
    const int32_t minimum = (int32_t)calibration->minimum_us;
    const int32_t maximum = (int32_t)calibration->maximum_us;
    const int32_t deadband = (int32_t)calibration->deadband_us;
    const int32_t lower_center = (int32_t)calibration->center_us - deadband;
    const int32_t upper_center = (int32_t)calibration->center_us + deadband;
    int32_t value = 0;

    if (pulse <= minimum) {
        value = -1000;
    } else if (pulse >= maximum) {
        value = 1000;
    } else if (pulse < lower_center) {
        value = ((pulse - lower_center) * 1000) / (lower_center - minimum);
    } else if (pulse > upper_center) {
        value = ((pulse - upper_center) * 1000) / (maximum - upper_center);
    } else {
        /* Inside the deadband: neutral. */
    }
    if (calibration->inverted) {
        value = -value;
    }
    return (int16_t)value;
}

void pwm_decoder_sample(const pwm_decoder_t *decoder,
                        const pwm_calibration_t calibration[PWM_RC_CHANNEL_COUNT], int64_t now_us,
                        int64_t timeout_us, pwm_rc_sample_t *sample)
{
    *sample = (pwm_rc_sample_t){.valid = true, .received_at_us = now_us};
    for (size_t index = 0U; index < PWM_RC_CHANNEL_COUNT; ++index) {
        const pwm_channel_t *const channel = &decoder->channels[index];
        sample->pulse_us[index] = channel->pulse_us;
        sample->period_us[index] = channel->period_us;
        sample->pulses += channel->pulses;
        sample->errors += channel->errors;
        if (!channel->valid || (now_us < channel->received_at_us) ||
            ((now_us - channel->received_at_us) >= timeout_us) ||
            !pwm_calibration_valid(&calibration[index])) {
            sample->valid = false;
        }
        if (channel->received_at_us < sample->received_at_us) {
            sample->received_at_us = channel->received_at_us;
        }
        if (channel->valid_since_us > sample->valid_since_us) {
            sample->valid_since_us = channel->valid_since_us;
        }
    }

    if (sample->valid) {
        for (size_t index = 0U; index < PWM_RC_CHANNEL_COUNT; ++index) {
            sample->channel_received_at_us[index] = decoder->channels[index].received_at_us;
            sample->normalized[index] = normalize(sample->pulse_us[index], &calibration[index]);
        }
    } else {
        sample->received_at_us = 0;
        sample->valid_since_us = 0;
    }
}
