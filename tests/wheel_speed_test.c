#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "wheel_speed.h"

static bool init_speed(wheel_speed_t *speed, unsigned counter_bits, bool inverted, uint32_t counter,
                       uint32_t now_ms)
{
    const wheel_speed_config_t config = {.counts_per_motor_rev = 4096U,
                                         .sample_ms = 10U,
                                         .max_gap_ms = 50U,
                                         .counter_bits = counter_bits,
                                         .inverted = inverted};
    return wheel_speed_init(speed, &config, counter, now_ms * 1000U);
}

/* Legacy scenarios specify milliseconds explicitly; production API uses us. */
static void update_ms(wheel_speed_t *speed, uint32_t counter, uint32_t now_ms)
{
    wheel_speed_update(speed, counter, now_ms * 1000U);
}

static wheel_speed_sample_t get_ms(const wheel_speed_t *speed, uint32_t now_ms)
{
    return wheel_speed_get(speed, now_ms * 1000U);
}

static void test_modular_counter_deltas(void)
{
    static const struct {
        uint32_t current;
        uint32_t previous;
        unsigned bits;
        int32_t delta;
    } cases[] = {{100U, 100U, 16U, 0},
                 {110U, 100U, 16U, 10},
                 {90U, 100U, 16U, -10},
                 {3U, 65530U, 16U, 9},
                 {65530U, 3U, 16U, -9},
                 {0U, UINT32_MAX, 32U, 1},
                 {UINT32_MAX, 0U, 32U, -1},
                 {4U, UINT32_MAX - 4U, 32U, 9},
                 {UINT32_MAX - 4U, 4U, 32U, -9},
                 {INT32_MAX, 0U, 32U, INT32_MAX},
                 {0U, INT32_MAX, 32U, -INT32_MAX}};
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        int32_t delta = 0;
        assert(
            wheel_speed_counter_delta(cases[i].current, cases[i].previous, cases[i].bits, &delta));
        assert(delta == cases[i].delta);
    }
    int32_t delta = 123;
    assert(!wheel_speed_counter_delta(32768U, 0U, 16U, &delta));
    assert(!wheel_speed_counter_delta(UINT32_C(0x80000000), 0U, 32U, &delta));
    assert(!wheel_speed_counter_delta(1U, 0U, 8U, &delta));
    assert(!wheel_speed_counter_delta(1U, 0U, 16U, NULL));
    assert(delta == 123);
}

static void test_speed_units_and_actual_time(void)
{
    wheel_speed_t speed;
    assert(init_speed(&speed, 16U, false, 0U, 0U));
    assert(get_ms(&speed, 0U).state == WHEEL_SPEED_WAITING);
    update_ms(&speed, 1000U, 9U);
    assert(get_ms(&speed, 9U).state == WHEEL_SPEED_WAITING);
    update_ms(&speed, 2048U, 10U);
    wheel_speed_sample_t sample = get_ms(&speed, 10U);
    assert(sample.state == WHEEL_SPEED_OK && sample.interval_ms == 10U);
    assert(sample.delta_counts == 2048);
    assert(sample.speed_mm_s == 2277);
    /* Same speed, double interval; do not assume every task update is 10 ms. */
    update_ms(&speed, 6144U, 30U);
    sample = get_ms(&speed, 30U);
    assert(sample.speed_mm_s == 2277);
    assert(sample.interval_ms == 20U);
    update_ms(&speed, 6144U, 40U);
    sample = get_ms(&speed, 40U);
    assert(sample.state == WHEEL_SPEED_OK && sample.speed_mm_s == 0);
}

static void test_inversion_and_both_wraps(void)
{
    const unsigned widths[] = {16U, 32U};
    for (size_t i = 0U; i < 2U; ++i) {
        wheel_speed_t speed;
        const uint32_t mask = (widths[i] == 16U) ? 65535U : UINT32_MAX;
        assert(init_speed(&speed, widths[i], false, mask - 100U, 0U));
        update_ms(&speed, 1947U, 10U);
        assert(get_ms(&speed, 10U).delta_counts == 2048);
        update_ms(&speed, mask - 100U, 20U);
        const wheel_speed_sample_t reverse = get_ms(&speed, 20U);
        assert(reverse.delta_counts == -2048 && reverse.speed_mm_s == -2277);
        assert(init_speed(&speed, widths[i], true, 2048U, 0U));
        update_ms(&speed, 0U, 10U);
        assert(get_ms(&speed, 10U).speed_mm_s == 2277);
    }
}

static void test_gaps_invalid_counts_and_recovery(void)
{
    wheel_speed_t speed;
    assert(init_speed(&speed, 16U, false, 0U, 0U));
    update_ms(&speed, 10000U, 10U);
    wheel_speed_sample_t sample = get_ms(&speed, 10U);
    assert(sample.state == WHEEL_SPEED_INVALID && sample.rejected == 1U);
    assert(sample.speed_mm_s == 0);
    update_ms(&speed, 12048U, 20U);
    assert(get_ms(&speed, 20U).speed_mm_s == 2277);
    update_ms(&speed, 14096U, 70U);
    sample = get_ms(&speed, 70U);
    assert(sample.state == WHEEL_SPEED_STALE && sample.speed_mm_s == 0);
    update_ms(&speed, 16144U, 80U);
    assert(get_ms(&speed, 80U).state == WHEEL_SPEED_OK);
    assert(get_ms(&speed, 129U).state == WHEEL_SPEED_OK);
    sample = get_ms(&speed, 130U);
    assert(sample.state == WHEEL_SPEED_STALE && sample.speed_mm_s == 0);
    assert(sample.age_ms == 50U);
}

static void test_clock_wrap_and_validation(void)
{
    wheel_speed_t speed;
    assert(init_speed(&speed, 32U, false, 0U, UINT32_MAX - 5U));
    update_ms(&speed, 2048U, 4U);
    assert(get_ms(&speed, 4U).speed_mm_s == 2277);
    assert(!init_speed(NULL, 32U, false, 0U, 0U));
    assert(!init_speed(&speed, 8U, false, 0U, 0U));
    update_ms(NULL, 0U, 0U);
    update_ms(&speed, 100U, 10U);
    assert(get_ms(&speed, 10U).state == WHEEL_SPEED_WAITING);
    assert(get_ms(NULL, 0U).state == WHEEL_SPEED_WAITING);
}

static void test_low_resolution_window_and_recovery(void)
{
    wheel_speed_config_t config = {.counts_per_motor_rev = 4U,
                                   .sample_ms = 200U,
                                   .max_gap_ms = 500U,
                                   .counter_bits = 32U,
                                   .inverted = true};
    wheel_speed_t speed;
    assert(wheel_speed_init(&speed, &config, 0U, 0U));
    /* Caller storage is not retained by the estimator. */
    config.counts_per_motor_rev = 4096U;
    update_ms(&speed, UINT32_MAX - 38U, 199U);
    assert(get_ms(&speed, 199U).state == WHEEL_SPEED_WAITING);
    update_ms(&speed, UINT32_MAX - 39U, 200U);
    wheel_speed_sample_t sample = get_ms(&speed, 200U);
    assert(sample.state == WHEEL_SPEED_OK && sample.interval_ms == 200U);
    assert(sample.delta_counts == 40 && sample.speed_mm_s == 2277);
    update_ms(&speed, 0U, 400U);
    sample = get_ms(&speed, 400U);
    assert(sample.delta_counts == -40 && sample.speed_mm_s == -2277);
    update_ms(&speed, 0U, 600U);
    assert(get_ms(&speed, 600U).speed_mm_s == 0);
    assert(get_ms(&speed, 1099U).state == WHEEL_SPEED_OK);
    assert(get_ms(&speed, 1100U).state == WHEEL_SPEED_STALE);
    update_ms(&speed, 0U, 1100U);
    assert(get_ms(&speed, 1100U).state == WHEEL_SPEED_STALE);
    update_ms(&speed, UINT32_MAX - 39U, 1300U);
    assert(get_ms(&speed, 1300U).speed_mm_s == 2277);

    config.counts_per_motor_rev = 4U;
    config.inverted = false;
    assert(wheel_speed_init(&speed, &config, 0U, 0U));
    update_ms(&speed, 82U, 200U);
    sample = get_ms(&speed, 200U);
    assert(sample.state == WHEEL_SPEED_INVALID && sample.rejected == 1U);
    update_ms(&speed, 122U, 400U);
    assert(get_ms(&speed, 400U).speed_mm_s == 2277);
    update_ms(&speed, 123U, 600U);
    sample = get_ms(&speed, 600U);
    assert(sample.speed_mm_s == 56);
}

static void test_configuration_limits(void)
{
    const wheel_speed_config_t valid = {.counts_per_motor_rev = 4096U,
                                        .sample_ms = 10U,
                                        .max_gap_ms = 50U,
                                        .counter_bits = 16U,
                                        .inverted = false};
    wheel_speed_t speed;
    assert(!wheel_speed_init(&speed, NULL, 0U, 0U));
    wheel_speed_config_t config = valid;
    config.counts_per_motor_rev = 0U;
    assert(!wheel_speed_init(&speed, &config, 0U, 0U));
    config.counts_per_motor_rev = 16385U;
    assert(!wheel_speed_init(&speed, &config, 0U, 0U));
    config = valid;
    config.sample_ms = 0U;
    assert(!wheel_speed_init(&speed, &config, 0U, 0U));
    config = valid;
    config.max_gap_ms = 10U;
    assert(!wheel_speed_init(&speed, &config, 0U, 0U));
    config = valid;
    config.counts_per_motor_rev = 8192U;
    assert(!wheel_speed_init(&speed, &config, 0U, 0U)); /* 16-bit ambiguity at 50 ms. */
    config.counter_bits = 32U;
    assert(wheel_speed_init(&speed, &config, 0U, 0U));
    config.max_gap_ms = UINT32_MAX;
    assert(!wheel_speed_init(&speed, &config, 0U, 0U));
    assert(get_ms(&speed, 0U).state == WHEEL_SPEED_WAITING);
}

static void test_submillisecond_intervals_and_microsecond_wrap(void)
{
    const wheel_speed_config_t config = {.counts_per_motor_rev = 4096U,
                                         .sample_ms = 10U,
                                         .max_gap_ms = 50U,
                                         .counter_bits = 16U,
                                         .inverted = false};
    wheel_speed_t speed;
    assert(wheel_speed_init(&speed, &config, 0U, 1000123U));
    /* 2560 counts in exactly 12.5 ms means 3000 RPM. Rounding the
     * interval to 12 ms would falsely report 3125 RPM. */
    wheel_speed_update(&speed, 2560U, 1012623U);
    wheel_speed_sample_t sample = wheel_speed_get(&speed, 1012623U);
    assert(sample.state == WHEEL_SPEED_OK && sample.interval_us == 12500U);
    assert(sample.interval_ms == 12U && sample.speed_mm_s == 2277);
    assert(sample.speed_mm_s == 2277);
    wheel_speed_update(&speed, 5120U, 1025123U);
    assert(wheel_speed_get(&speed, 1025123U).speed_mm_s == 2277);

    assert(wheel_speed_init(&speed, &config, 0U, UINT32_MAX - 4999U));
    wheel_speed_update(&speed, 3072U, 10000U);
    sample = wheel_speed_get(&speed, 10000U);
    assert(sample.interval_us == 15000U && sample.speed_mm_s == 2277);

    assert(wheel_speed_init(&speed, &config, 0U, 0U));
    wheel_speed_update(&speed, 2047U, 9999U);
    assert(wheel_speed_get(&speed, 9999U).state == WHEEL_SPEED_WAITING);
    wheel_speed_update(&speed, 2048U, 10000U);
    assert(wheel_speed_get(&speed, 10000U).speed_mm_s == 2277);
    assert(wheel_speed_get(&speed, 59999U).state == WHEEL_SPEED_OK);
    assert(wheel_speed_get(&speed, 60000U).state == WHEEL_SPEED_STALE);

    wheel_speed_config_t too_long = config;
    too_long.counter_bits = 32U;
    too_long.max_gap_ms = 1001U;
    assert(!wheel_speed_init(&speed, &too_long, 0U, 0U));
}

int main(void)
{
    test_modular_counter_deltas();
    test_speed_units_and_actual_time();
    test_inversion_and_both_wraps();
    test_gaps_invalid_counts_and_recovery();
    test_clock_wrap_and_validation();
    test_low_resolution_window_and_recovery();
    test_configuration_limits();
    test_submillisecond_intervals_and_microsecond_wrap();
    return 0;
}
