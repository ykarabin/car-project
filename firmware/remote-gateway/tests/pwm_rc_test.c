#undef NDEBUG
#include <assert.h>
#include <string.h>

#include "pwm_decoder.h"
#include "remote_input.h"

#define TEST_TIMEOUT_US INT64_C(100000)
#define TEST_START_US INT64_C(1000000)

static const pwm_calibration_t CALIBRATION[PWM_RC_CHANNEL_COUNT] = {
    {1000U, 1500U, 2000U, 20U, false},
    {1000U, 1500U, 2000U, 20U, false},
    {1000U, 1500U, 2000U, 0U, false}};

static uint32_t capture_ticks(int64_t time_us)
{
    return (uint32_t)((uint64_t)time_us * UINT64_C(80));
}

static void pulse(pwm_decoder_t *decoder, size_t channel, int64_t rise_us, uint32_t width_us)
{
    pwm_decoder_capture(decoder, channel, true, capture_ticks(rise_us), rise_us);
    const int64_t fall_us = rise_us + (int64_t)width_us;
    pwm_decoder_capture(decoder, channel, false, capture_ticks(fall_us), fall_us);
}

static void frame(pwm_decoder_t *decoder, int64_t start_us,
                  const uint32_t widths[PWM_RC_CHANNEL_COUNT])
{
    for (size_t channel = 0U; channel < PWM_RC_CHANNEL_COUNT; ++channel) {
        pulse(decoder, channel, start_us + (int64_t)(channel * 100U), widths[channel]);
    }
}

static int64_t prime(pwm_decoder_t *decoder, int64_t start_us, uint32_t period_us,
                     const uint32_t widths[PWM_RC_CHANNEL_COUNT])
{
    for (uint32_t index = 0U; index <= PWM_RC_CONFIRM_PULSES; ++index) {
        frame(decoder, start_us + (int64_t)(index * period_us), widths);
    }
    return start_us + (int64_t)(PWM_RC_CONFIRM_PULSES * period_us) + 2800;
}

static void test_startup_and_normalization(void)
{
    pwm_decoder_t decoder = {.ticks_per_us = 80U};
    pwm_rc_sample_t sample;
    pwm_decoder_sample(&decoder, CALIBRATION, TEST_START_US, TEST_TIMEOUT_US, &sample);
    assert(!sample.valid);
    assert(sample.normalized[0] == 0 && sample.received_at_us == 0);

    /* An initial falling edge and one apparently valid pulse cannot acquire input. */
    pwm_decoder_capture(&decoder, 0U, false, 0U, TEST_START_US - 100);
    const uint32_t widths[] = {1000U, 1500U, 2000U};
    frame(&decoder, TEST_START_US, widths);
    pwm_decoder_sample(&decoder, CALIBRATION, TEST_START_US + 2800, TEST_TIMEOUT_US, &sample);
    assert(!sample.valid && sample.errors == 0U);

    for (uint32_t index = 1U; index <= PWM_RC_CONFIRM_PULSES; ++index) {
        frame(&decoder, TEST_START_US + (int64_t)(index * 20000U), widths);
        pwm_decoder_sample(&decoder, CALIBRATION, TEST_START_US + (int64_t)(index * 20000U) + 2800,
                           TEST_TIMEOUT_US, &sample);
        assert(sample.valid == (index == PWM_RC_CONFIRM_PULSES));
    }
    assert(sample.normalized[0] == -1000);
    assert(sample.normalized[1] == 0);
    assert(sample.normalized[2] == 1000);
    assert(sample.received_at_us == TEST_START_US + 61000);
    assert(sample.valid_since_us == TEST_START_US + 62200);
    assert(sample.pulses == 9U && sample.errors == 0U);
}

static void test_any_channel_timeout(void)
{
    const uint32_t widths[] = {1500U, 1500U, 1500U};
    for (size_t missing = 0U; missing < PWM_RC_CHANNEL_COUNT; ++missing) {
        pwm_decoder_t decoder = {.ticks_per_us = 80U};
        (void)prime(&decoder, TEST_START_US, 20000U, widths);
        for (uint32_t index = 4U; index <= 8U; ++index) {
            for (size_t channel = 0U; channel < PWM_RC_CHANNEL_COUNT; ++channel) {
                if (channel != missing) {
                    pulse(&decoder, channel,
                          TEST_START_US + (int64_t)(index * 20000U) + (int64_t)(channel * 100U),
                          1500U);
                }
            }
        }
        pwm_rc_sample_t sample;
        pwm_decoder_sample(&decoder, CALIBRATION, TEST_START_US + 163000, TEST_TIMEOUT_US, &sample);
        assert(!sample.valid && sample.received_at_us == 0 && sample.valid_since_us == 0);
        for (size_t channel = 0U; channel < PWM_RC_CHANNEL_COUNT; ++channel) {
            assert(sample.normalized[channel] == 0);
        }
    }
}

static void test_throttle_endpoint_calibration(void)
{
    pwm_decoder_t decoder = {.ticks_per_us = 80U};
    const uint32_t widths[] = {1980U, 1980U, 2000U};
    const int64_t now_us = prime(&decoder, TEST_START_US, 20000U, widths);
    pwm_rc_sample_t sample;
    pwm_decoder_sample(&decoder, CALIBRATION, now_us, TEST_TIMEOUT_US, &sample);
    assert(sample.valid && sample.normalized[1] == 958); /* Reported RC endpoint. */

    pwm_calibration_t calibration[PWM_RC_CHANNEL_COUNT];
    (void)memcpy(calibration, CALIBRATION, sizeof(calibration));
    calibration[1].maximum_us = 1980U;
    pwm_decoder_sample(&decoder, calibration, now_us, TEST_TIMEOUT_US, &sample);
    assert(sample.valid && sample.normalized[1] == 1000);
    assert(sample.normalized[0] == 958 && sample.normalized[2] == 1000);
    const remote_command_t command = remote_command_from_input(sample.valid, sample.normalized[0],
                                                               sample.normalized[1], 3U, 42U);
    assert(command.status == REMOTE_STATUS_OK && command.throttle == 1000);
    assert(command.steering == 958 && command.drive_mode == 3U);

    const struct {
        uint32_t pulse;
        int16_t normalized;
    } cases[] = {{1000U, -1000}, {1240U, -500}, {1480U, 0},    {1500U, 0},
                 {1520U, 0},     {1750U, 500},  {1980U, 1000}, {2000U, 1000}};
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        decoder = (pwm_decoder_t){.ticks_per_us = 80U};
        const uint32_t pulse_widths[] = {1500U, cases[i].pulse, 1500U};
        const int64_t at_us = prime(&decoder, TEST_START_US, 20000U, pulse_widths);
        calibration[1].inverted = false;
        pwm_decoder_sample(&decoder, calibration, at_us, TEST_TIMEOUT_US, &sample);
        assert(sample.valid && sample.normalized[1] == cases[i].normalized);
        assert(sample.normalized[0] == 0 && sample.normalized[2] == 0);
        calibration[1].inverted = true;
        pwm_decoder_sample(&decoder, calibration, at_us, TEST_TIMEOUT_US, &sample);
        assert(sample.valid && sample.normalized[1] == -cases[i].normalized);
    }
}

static void test_pulse_and_period_boundaries(void)
{
    const uint32_t widths_to_check[] = {799U, 800U, 2200U, 2201U};
    for (size_t channel = 0U; channel < PWM_RC_CHANNEL_COUNT; ++channel) {
        for (size_t index = 0U; index < 4U; ++index) {
            uint32_t widths[] = {1500U, 1500U, 1500U};
            widths[channel] = widths_to_check[index];
            pwm_decoder_t decoder = {.ticks_per_us = 80U};
            const int64_t now_us = prime(&decoder, TEST_START_US, 20000U, widths);
            pwm_rc_sample_t sample;
            pwm_decoder_sample(&decoder, CALIBRATION, now_us, TEST_TIMEOUT_US, &sample);
            assert(sample.valid == ((index == 1U) || (index == 2U)));
        }
    }
    const uint32_t periods[] = {2999U, 3000U, 50000U, 50001U};
    const uint32_t widths[] = {1500U, 1500U, 1500U};
    for (size_t index = 0U; index < 4U; ++index) {
        pwm_decoder_t decoder = {.ticks_per_us = 80U};
        const int64_t now_us = prime(&decoder, TEST_START_US, periods[index], widths);
        pwm_rc_sample_t sample;
        pwm_decoder_sample(&decoder, CALIBRATION, now_us, TEST_TIMEOUT_US, &sample);
        assert(sample.valid == ((index == 1U) || (index == 2U)));
    }
}

static void test_error_and_recovery(void)
{
    pwm_decoder_t decoder = {.ticks_per_us = 80U};
    const uint32_t widths[] = {1500U, 1500U, 1500U};
    (void)prime(&decoder, TEST_START_US, 20000U, widths);
    const uint32_t invalid_widths[] = {2500U, 1500U, 1500U};
    frame(&decoder, TEST_START_US + 80000, invalid_widths);
    pwm_rc_sample_t sample;
    pwm_decoder_sample(&decoder, CALIBRATION, TEST_START_US + 83000, TEST_TIMEOUT_US, &sample);
    assert(!sample.valid && sample.errors == 1U && sample.pulse_us[0] == 2500U);

    for (uint32_t index = 5U; index <= 7U; ++index) {
        frame(&decoder, TEST_START_US + (int64_t)(index * 20000U), widths);
        pwm_decoder_sample(&decoder, CALIBRATION, TEST_START_US + (int64_t)(index * 20000U) + 2800,
                           TEST_TIMEOUT_US, &sample);
        assert(sample.valid == (index == 7U));
    }
    pwm_decoder_capture(&decoder, 0U, false, capture_ticks(TEST_START_US + 145000),
                        TEST_START_US + 145000);
    pwm_decoder_sample(&decoder, CALIBRATION, TEST_START_US + 145000, TEST_TIMEOUT_US, &sample);
    assert(!sample.valid && sample.errors == 2U);
}

static void test_counter_wrap_and_long_pause(void)
{
    pwm_decoder_t decoder = {.ticks_per_us = 80U};
    const uint32_t widths[] = {1500U, 1500U, 1500U};
    const int64_t start_us = (int64_t)(UINT32_MAX / 80U) - 10000;
    const int64_t now_us = prime(&decoder, start_us, 20000U, widths);
    pwm_rc_sample_t sample;
    pwm_decoder_sample(&decoder, CALIBRATION, now_us, TEST_TIMEOUT_US, &sample);
    assert(sample.valid && sample.errors == 0U);

    /* Similar counter values after a full wrap cannot make old input fresh. */
    const int64_t later_us = now_us + (int64_t)(UINT32_MAX / 80U) + 1;
    frame(&decoder, later_us, widths);
    pwm_decoder_sample(&decoder, CALIBRATION, later_us + 2800, TEST_TIMEOUT_US, &sample);
    assert(!sample.valid);
}

static void test_deadband_inversion_and_bad_configuration(void)
{
    pwm_decoder_t decoder = {.ticks_per_us = 80U};
    const uint32_t widths[] = {1480U, 1520U, 1500U};
    int64_t now_us = prime(&decoder, TEST_START_US, 20000U, widths);
    pwm_rc_sample_t sample;
    pwm_decoder_sample(&decoder, CALIBRATION, now_us, TEST_TIMEOUT_US, &sample);
    assert(sample.valid && sample.normalized[0] == 0 && sample.normalized[1] == 0);

    pwm_calibration_t calibration[PWM_RC_CHANNEL_COUNT];
    (void)memcpy(calibration, CALIBRATION, sizeof(calibration));
    calibration[0].inverted = true;
    calibration[1].inverted = true;
    const uint32_t endpoints[] = {1000U, 2000U, 1500U};
    frame(&decoder, TEST_START_US + 80000, endpoints);
    now_us = TEST_START_US + 83000;
    pwm_decoder_sample(&decoder, calibration, now_us, TEST_TIMEOUT_US, &sample);
    assert(sample.valid && sample.normalized[0] == 1000 && sample.normalized[1] == -1000);

    calibration[0].center_us = calibration[0].minimum_us;
    assert(!pwm_calibration_valid(&calibration[0]));
    pwm_decoder_sample(&decoder, calibration, now_us, TEST_TIMEOUT_US, &sample);
    assert(!sample.valid);
    calibration[0] = CALIBRATION[0];
    calibration[0].deadband_us = 500U;
    assert(!pwm_calibration_valid(&calibration[0]));
}

static void test_mode_confirmation_uses_ch3_pulses(void)
{
    pwm_decoder_t decoder = {.ticks_per_us = 80U};
    const uint32_t widths[] = {1500U, 1500U, 1500U};
    int64_t now_us = prime(&decoder, TEST_START_US, 20000U, widths);
    pwm_rc_sample_t sample;
    pwm_decoder_sample(&decoder, CALIBRATION, now_us, TEST_TIMEOUT_US, &sample);
    remote_mode_cycle_t mode = {0};
    assert(remote_mode_cycle_update(&mode, sample.valid, sample.normalized[2],
                                    sample.channel_received_at_us[2]) == 1U);

    pulse(&decoder, 2U, TEST_START_US + 105000, 2000U);
    pwm_decoder_sample(&decoder, CALIBRATION, TEST_START_US + 107100, TEST_TIMEOUT_US, &sample);
    assert(sample.valid);
    assert(remote_mode_cycle_update(&mode, sample.valid, sample.normalized[2],
                                    sample.channel_received_at_us[2]) == 1U);

    /* Other channels catching up must not confirm a single changed CH3 pulse. */
    pulse(&decoder, 0U, TEST_START_US + 106000, 1500U);
    pulse(&decoder, 1U, TEST_START_US + 106100, 1500U);
    pwm_decoder_sample(&decoder, CALIBRATION, TEST_START_US + 108000, TEST_TIMEOUT_US, &sample);
    assert(sample.valid && sample.channel_received_at_us[2] == TEST_START_US + 107000);
    assert(remote_mode_cycle_update(&mode, sample.valid, sample.normalized[2],
                                    sample.channel_received_at_us[2]) == 1U);

    pulse(&decoder, 2U, TEST_START_US + 125000, 2000U);
    pwm_decoder_sample(&decoder, CALIBRATION, TEST_START_US + 127100, TEST_TIMEOUT_US, &sample);
    assert(remote_mode_cycle_update(&mode, sample.valid, sample.normalized[2],
                                    sample.channel_received_at_us[2]) == 1U);
    pulse(&decoder, 2U, TEST_START_US + 145000, 2000U);
    pwm_decoder_sample(&decoder, CALIBRATION, TEST_START_US + 147100, TEST_TIMEOUT_US, &sample);
    assert(remote_mode_cycle_update(&mode, sample.valid, sample.normalized[2],
                                    sample.channel_received_at_us[2]) == 2U);
}

int main(void)
{
    test_startup_and_normalization();
    test_any_channel_timeout();
    test_throttle_endpoint_calibration();
    test_pulse_and_period_boundaries();
    test_error_and_recovery();
    test_counter_wrap_and_long_pause();
    test_deadband_inversion_and_bad_configuration();
    test_mode_confirmation_uses_ch3_pulses();
    return 0;
}
