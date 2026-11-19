#ifndef VEHICLE_WHEEL_SPEED_H
#define VEHICLE_WHEEL_SPEED_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WHEEL_SPEED_WAITING,
    WHEEL_SPEED_OK,
    WHEEL_SPEED_INVALID,
    WHEEL_SPEED_STALE
} wheel_speed_state_t;

typedef struct {
    uint32_t counter;
    uint32_t interval_ms;
    uint32_t interval_us;
    uint32_t age_ms;
    uint32_t rejected;
    int32_t delta_counts;
    int32_t motor_rpm_milli;
    int32_t wheel_rpm_milli;
    int32_t speed_mm_s;
    wheel_speed_state_t state;
} wheel_speed_sample_t;

typedef struct {
    uint32_t counts_per_motor_rev;
    uint32_t sample_ms;
    uint32_t max_gap_ms;
    unsigned counter_bits;
    bool inverted;
} wheel_speed_config_t;

typedef struct {
    wheel_speed_sample_t sample;
    wheel_speed_config_t config;
    uint32_t updated_at_us;
    bool initialized;
} wheel_speed_t;

/* Defined modular subtraction for 16/32-bit timers. Rejects exactly half a
 * counter range because its direction is ambiguous. No unsigned->signed wrap. */
bool wheel_speed_counter_delta(uint32_t current, uint32_t previous,
                                unsigned counter_bits, int32_t *delta);
/* Configuration is validated and copied. At the maximum plausible RPM,
 * its max gap must keep the modular counter delta below half the range.
 * Supported windows are <=1000 ms to bound fixed-point products. */
bool wheel_speed_init(wheel_speed_t *speed, const wheel_speed_config_t *config,
                       uint32_t counter, uint32_t now_us);
/* Called in one task; uses each wheel's sample period and actual elapsed time.
 * Gaps >=max_gap_ms and implausible deltas invalidate speed and re-seed CNT. */
void wheel_speed_update(wheel_speed_t *speed, uint32_t counter, uint32_t now_us);
wheel_speed_sample_t wheel_speed_get(const wheel_speed_t *speed, uint32_t now_us);
const char *wheel_speed_state_name(wheel_speed_state_t state);

#endif
