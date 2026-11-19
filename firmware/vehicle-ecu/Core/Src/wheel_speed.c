#include "wheel_speed.h"

#include <stddef.h>

#include "rear_encoder_config.h"

bool wheel_speed_counter_delta(uint32_t current, uint32_t previous,
                                unsigned counter_bits, int32_t *delta)
{
    if ((delta == NULL) || ((counter_bits != 16U) && (counter_bits != 32U))) {
        return false;
    }
    const uint32_t mask = (counter_bits == 16U) ? UINT32_C(65535) : UINT32_MAX;
    const uint32_t half = (counter_bits == 16U) ? UINT32_C(32768) : UINT32_C(0x80000000);
    const uint32_t difference = (current - previous) & mask;
    if (difference == half) {
        return false;
    }
    *delta = (difference < half) ? (int32_t)difference : -(int32_t)((mask - difference) + 1U);
    return true;
}

bool wheel_speed_init(wheel_speed_t *speed, const wheel_speed_config_t *config,
                       uint32_t counter, uint32_t now_us)
{
    if (speed == NULL) {
        return false;
    }
    *speed = (wheel_speed_t){0};
    /* MT6826S supports up to 4096 PPR, decoded x4. The bound also keeps
     * configuration validation and later 64-bit products within range. */
    if ((config == NULL) ||
        ((config->counter_bits != 16U) && (config->counter_bits != 32U)) ||
        (config->counts_per_motor_rev == 0U) ||
        (config->counts_per_motor_rev > 16384U) || (config->sample_ms == 0U) ||
        (config->max_gap_ms <= config->sample_ms) || (config->max_gap_ms > 1000U)) {
        return false;
    }
    const uint64_t maximum_counts =
        ((uint64_t)REAR_ENCODER_MAX_MOTOR_RPM * config->counts_per_motor_rev *
         config->max_gap_ms) / 60000U + 1U;
    const uint64_t half_range = (config->counter_bits == 16U) ?
                                UINT64_C(32768) : UINT64_C(2147483648);
    if (maximum_counts >= half_range) {
        return false;
    }
    speed->config = *config;
    speed->sample.counter = (config->counter_bits == 16U) ? counter & UINT32_C(65535) : counter;
    speed->updated_at_us = now_us;
    speed->initialized = true;
    return true;
}

static void clear_velocity(wheel_speed_sample_t *sample)
{
    sample->delta_counts = 0;
    sample->motor_rpm_milli = 0;
    sample->wheel_rpm_milli = 0;
    sample->speed_mm_s = 0;
}

void wheel_speed_update(wheel_speed_t *speed, uint32_t counter, uint32_t now_us)
{
    if ((speed == NULL) || !speed->initialized) {
        return;
    }
    const uint32_t elapsed = now_us - speed->updated_at_us;
    if (elapsed < speed->config.sample_ms * 1000U) {
        return;
    }
    int32_t delta = 0;
    const bool unambiguous = wheel_speed_counter_delta(counter, speed->sample.counter,
                                                       speed->config.counter_bits, &delta);
    speed->sample.counter = (speed->config.counter_bits == 16U) ? counter & UINT32_C(65535) : counter;
    speed->sample.interval_us = elapsed;
    speed->sample.interval_ms = elapsed / 1000U;
    speed->updated_at_us = now_us;
    clear_velocity(&speed->sample);
    if (elapsed >= speed->config.max_gap_ms * 1000U) {
        speed->sample.state = WHEEL_SPEED_STALE;
        ++speed->sample.rejected;
        return;
    }
    const int64_t maximum = ((int64_t)REAR_ENCODER_MAX_MOTOR_RPM *
                              speed->config.counts_per_motor_rev * elapsed) / 60000000 + 1;
    if (!unambiguous || ((int64_t)delta > maximum) || ((int64_t)delta < -maximum)) {
        speed->sample.state = WHEEL_SPEED_INVALID;
        ++speed->sample.rejected;
        return;
    }
    if (speed->config.inverted) {
        delta = -delta;
    }
    const int64_t motor_denominator = (int64_t)speed->config.counts_per_motor_rev * elapsed;
    const int64_t wheel_denominator = motor_denominator * REAR_ENCODER_GEAR_RATIO;
    speed->sample.delta_counts = delta;
    speed->sample.motor_rpm_milli = (int32_t)(((int64_t)delta * INT64_C(60000000000)) / motor_denominator);
    speed->sample.wheel_rpm_milli = (int32_t)(((int64_t)delta * INT64_C(60000000000)) / wheel_denominator);
    speed->sample.speed_mm_s = (int32_t)(((int64_t)delta * REAR_ENCODER_WHEEL_CIRCUMFERENCE_UM * 1000) /
                                        wheel_denominator);
    speed->sample.state = WHEEL_SPEED_OK;
}

wheel_speed_sample_t wheel_speed_get(const wheel_speed_t *speed, uint32_t now_us)
{
    if ((speed == NULL) || !speed->initialized) {
        return (wheel_speed_sample_t){0};
    }
    wheel_speed_sample_t sample = speed->sample;
    const uint32_t age_us = now_us - speed->updated_at_us;
    sample.age_ms = age_us / 1000U;
    if (age_us >= speed->config.max_gap_ms * 1000U) {
        sample.state = WHEEL_SPEED_STALE;
        clear_velocity(&sample);
    }
    return sample;
}

const char *wheel_speed_state_name(wheel_speed_state_t state)
{
    switch (state) {
    case WHEEL_SPEED_WAITING: return "WAITING";
    case WHEEL_SPEED_OK: return "OK";
    case WHEEL_SPEED_INVALID: return "INVALID";
    case WHEEL_SPEED_STALE: return "STALE";
    default: return "UNKNOWN";
    }
}
