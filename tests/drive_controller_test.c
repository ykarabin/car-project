#undef NDEBUG
#include <assert.h>
#include <limits.h>
#include <stddef.h>

#include "drive_controller.h"
#include "drive_controller_config.h"

static remote_command_t command(int16_t throttle, uint8_t mode)
{
    return (remote_command_t){.throttle = throttle, .drive_mode = mode,
                              .status = REMOTE_STATUS_OK};
}

static wheel_speed_sample_t feedback(int32_t speed)
{
    return (wheel_speed_sample_t){.state = WHEEL_SPEED_OK, .interval_ms = 10U,
                                  .speed_mm_s = speed};
}

static void prepare_reverse_from_rest(drive_controller_t *controller,
                                      remote_command_t *input,
                                      const wheel_speed_sample_t *right)
{
    input->throttle = -1000;
    assert(drive_controller_update(controller, input, right, 0U) == 0);
    assert(controller->state == DRIVE_CONTROL_BRAKING);
    input->throttle = 0;
    assert(drive_controller_update(controller, input, right, 10U) == 0);
    assert(controller->reverse_stage == DRIVE_REVERSE_READY);
    input->throttle = -1000;
    assert(drive_controller_update(controller, input, right, 20U) == 0);
    for (uint32_t now = 30U; now <= 120U; now += 10U) {
        assert(drive_controller_update(controller, input, right, now) == 0);
        assert(controller->state == DRIVE_CONTROL_REVERSING);
    }
    assert(controller->reverse_stage == DRIVE_REVERSE_ACTIVE);
}

static void test_mode_mapping_and_deadband(void)
{
    const int32_t maximums[] = {1111, 1666, DRIVE_CONTROL_NOMINAL_MM_S};
    wheel_speed_sample_t right = feedback(0);
    for (uint8_t mode = 1U; mode <= 3U; ++mode) {
        for (int32_t throttle = -1000; throttle <= 1000; ++throttle) {
            drive_controller_t controller = {0};
            remote_command_t input = command((int16_t)throttle, mode);
            assert(drive_controller_update(&controller, &input, &right, 0U) == 0);
            const int32_t magnitude = (throttle < 0) ? -throttle : throttle;
            const int32_t expected = (magnitude <= 30) ? 0 :
                                     ((magnitude - 30) * maximums[mode - 1U] / 970);
            assert(controller.target_mm_s == ((throttle < 0) ? -expected : expected));
            assert(controller.full_power_requested == ((mode == 3U) && (magnitude >= 990)));
            if (mode < 3U) assert(expected <= maximums[mode - 1U]);
        }
    }
}

static void test_full_mode_and_downshift(void)
{
    drive_controller_t controller = {0};
    remote_command_t input = command(1000, 3U);
    wheel_speed_sample_t right = feedback(0);
    assert(drive_controller_update(&controller, &input, &right, 0U) == 0);
    for (uint32_t now = 10U; now <= 1000U; now += 10U) {
        const int16_t pwm = drive_controller_update(&controller, &input, &right, now);
        assert(pwm == (int16_t)now);
        assert(controller.state == DRIVE_CONTROL_FULL && controller.integral_milli_pwm == 0);
    }
    right.speed_mm_s = 3000; /* Mode 3 full power is not capped at nominal speed. */
    assert(drive_controller_update(&controller, &input, &right, 1010U) == 1000);
    input.drive_mode = 1U;
    int16_t previous = drive_controller_update(&controller, &input, &right, 1020U);
    assert(previous == 990); /* Mode change must not drop full power in one step. */
    assert(controller.target_mm_s == 1111 && !controller.full_power_requested);
    assert(controller.state == DRIVE_CONTROL_TRACKING);
    for (uint32_t now = 1030U; now <= 4020U; now += 10U) {
        const int16_t pwm = drive_controller_update(&controller, &input, &right, now);
        assert(pwm >= 0 && pwm <= previous && (previous - pwm) <= 10);
        previous = pwm;
    }
    assert(controller.pwm == 0);

    drive_controller_stop(&controller);
    input = command(-1000, 3U);
    right = feedback(0);
    prepare_reverse_from_rest(&controller, &input, &right);
    for (uint32_t now = 130U; now <= 1120U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == -(int16_t)(now - 120U));
    }
    input.throttle = 0;
    assert(drive_controller_update(&controller, &input, &right, 1121U) == -1000);
    assert(controller.state == DRIVE_CONTROL_STOPPING);
    for (uint32_t now = 1130U; now <= 2120U; now += 10U) {
        const int16_t expected = (int16_t)(-(int32_t)(1000U - (now - 1120U)));
        assert(drive_controller_update(&controller, &input, &right, now) == expected);
    }
    assert(controller.integral_milli_pwm == 0 && controller.state == DRIVE_CONTROL_STOP);
}

static void test_reversal_and_opposite_startup_motion(void)
{
    drive_controller_t controller = {0};
    remote_command_t input = command(1000, 3U);
    wheel_speed_sample_t right = feedback(500);
    (void)drive_controller_update(&controller, &input, &right, 0U);
    for (uint32_t now = 10U; now <= 300U; now += 10U) {
        (void)drive_controller_update(&controller, &input, &right, now);
    }
    assert(controller.pwm == 300);
    input.throttle = -1000;
    assert(drive_controller_update(&controller, &input, &right, 301U) == 300);
    for (uint32_t now = 310U; now <= 450U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) ==
               (int16_t)(300U - (now - 300U) * 2U));
        assert(controller.state == DRIVE_CONTROL_BRAKING);
    }
    right.speed_mm_s = 0;
    for (uint32_t now = 460U; now <= 1000U; now += 10U) {
        /* Holding brake, even after stopping and changing modes, never reverses. */
        input.drive_mode = (uint8_t)(1U + ((now / 10U) % 3U));
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
        assert(controller.state == DRIVE_CONTROL_BRAKING);
        assert(controller.reverse_stage == DRIVE_REVERSE_WAIT_NEUTRAL);
    }
    input.throttle = -30; /* Neutral boundary is a valid release. */
    assert(drive_controller_update(&controller, &input, &right, 1010U) == 0);
    input = command(-1000, 3U);
    assert(drive_controller_update(&controller, &input, &right, 1020U) == 0);
    right.speed_mm_s = 500; /* A quick second pull still cannot reverse moving wheels. */
    for (uint32_t now = 1030U; now <= 1200U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
    }
    right.speed_mm_s = 0;
    for (uint32_t now = 1210U; now <= 1300U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
    }
    assert(drive_controller_update(&controller, &input, &right, 1310U) == -10);

    /* Remote loss can erase the previous command direction. Actual opposite
     * wheel motion must still require a settled stop before new drive. */
    drive_controller_stop(&controller);
    right.speed_mm_s = 500;
    (void)drive_controller_update(&controller, &input, &right, 0U);
    assert(drive_controller_update(&controller, &input, &right, 10U) == 0);
    assert(controller.state == DRIVE_CONTROL_BRAKING);
    input.throttle = 0;
    assert(drive_controller_update(&controller, &input, &right, 20U) == 0);
    input.throttle = -1000;
    assert(drive_controller_update(&controller, &input, &right, 30U) == 0);
    assert(drive_controller_update(&controller, &input, &right, 40U) == 0);
    right.speed_mm_s = 0;
    for (uint32_t now = 50U; now <= 140U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
        assert(controller.state == DRIVE_CONTROL_REVERSING);
    }
    assert(drive_controller_update(&controller, &input, &right, 150U) < 0);
}

static void test_invalid_feedback_input_and_timing(void)
{
    const wheel_speed_state_t bad_states[] = {WHEEL_SPEED_WAITING, WHEEL_SPEED_INVALID, WHEEL_SPEED_STALE};
    remote_command_t input = command(1000, 3U);
    wheel_speed_sample_t right = feedback(0);
    drive_controller_t controller = {0};
    assert(drive_controller_update(NULL, &input, &right, 0U) == 0);
    for (size_t i = 0U; i < sizeof(bad_states) / sizeof(bad_states[0]); ++i) {
        (void)drive_controller_update(&controller, &input, &right, 0U);
        assert(drive_controller_update(&controller, &input, &right, 10U) > 0);
        right.state = bad_states[i];
        assert(drive_controller_update(&controller, &input, &right, 11U) == 10);
        assert(controller.state == DRIVE_CONTROL_FEEDBACK_HOLD);
        assert(drive_controller_update(&controller, &input, &right, 50U) == 0);
        assert(controller.state == DRIVE_CONTROL_FEEDBACK_ERROR);
        right = feedback(0);
    }
    assert(drive_controller_update(&controller, &input, NULL, 12U) == 0);
    right.age_ms = 20U;
    assert(drive_controller_update(&controller, &input, &right, 13U) == 0);
    right = feedback(INT32_MIN);
    assert(drive_controller_update(&controller, &input, &right, 14U) == 0);
    right = feedback(INT32_MAX);
    assert(drive_controller_update(&controller, &input, &right, 15U) == 0);
    right = feedback(0);
    right.interval_ms = 50U;
    assert(drive_controller_update(&controller, &input, &right, 16U) == 0);
    right = feedback(0);
    input.drive_mode = 0U;
    assert(drive_controller_update(&controller, &input, &right, 17U) == 0);
    assert(controller.state == DRIVE_CONTROL_DISABLED);
    input = command(1001, 1U);
    assert(drive_controller_update(&controller, &input, &right, 18U) == 0);
    input = command(1000, 1U);
    input.status = REMOTE_STATUS_ERROR;
    assert(drive_controller_update(&controller, &input, &right, 19U) == 0);
    assert(drive_controller_update(&controller, NULL, &right, 20U) == 0);

    input = command(1000, 3U);
    assert(drive_controller_update(&controller, &input, &right, UINT32_MAX - 5U) == 0);
    assert(drive_controller_update(&controller, &input, &right, 4U) == 10);
    assert(drive_controller_update(&controller, &input, &right, 54U) == 0);
    assert(controller.state == DRIVE_CONTROL_WAITING && controller.integral_milli_pwm == 0);
    assert(drive_controller_update(&controller, &input, &right, 64U) == 10);
}

static void test_integrator_saturation_and_fractional_errors(void)
{
    drive_controller_t controller = {0};
    remote_command_t input = command(1000, 1U);
    wheel_speed_sample_t right = feedback(0);
    (void)drive_controller_update(&controller, &input, &right, 0U);
    for (uint32_t now = 10U; now <= 10000U; now += 10U) {
        const int16_t output = drive_controller_update(&controller, &input, &right, now);
        assert(output >= 0 && output <= 1000);
    }
    assert(controller.pwm == 1000);
    const int32_t saturated_integral = controller.integral_milli_pwm;
    assert(saturated_integral > 0 && saturated_integral < 700000);
    for (uint32_t now = 10010U; now <= 11000U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 1000);
        assert(controller.integral_milli_pwm == saturated_integral);
    }
    right.speed_mm_s = 3000;
    for (uint32_t now = 11010U; now <= 14000U; now += 10U) {
        (void)drive_controller_update(&controller, &input, &right, now);
    }
    assert(controller.pwm == 0);

    drive_controller_stop(&controller);
    input = command(52, 1U); /* Target 25 mm/s, error exactly 1 mm/s. */
    right = feedback(24);
    (void)drive_controller_update(&controller, &input, &right, 0U);
    (void)drive_controller_update(&controller, &input, &right, 10U);
    (void)drive_controller_update(&controller, &input, &right, 20U);
    const int32_t initial_integral = controller.integral_milli_pwm;
    for (uint32_t now = 30U; now <= 2020U; now += 10U) {
        (void)drive_controller_update(&controller, &input, &right, now);
    }
    assert(controller.target_mm_s == 25 && controller.error_mm_s == 1);
    assert(controller.integral_milli_pwm - initial_integral == 600);
}

static void test_mode_changes_preserve_integrator_and_slew_both_directions(void)
{
    drive_controller_t controller = {0};
    remote_command_t input = command(1000, 1U);
    wheel_speed_sample_t right = feedback(700);
    (void)drive_controller_update(&controller, &input, &right, 0U);
    for (uint32_t now = 10U; now <= 2000U; now += 10U) {
        (void)drive_controller_update(&controller, &input, &right, now);
    }
    assert(controller.integral_milli_pwm > 0);
    const int32_t integral = controller.integral_milli_pwm;
    const int16_t before = controller.pwm;
    input.drive_mode = 2U;
    /* No new control period: mode selection must retain the learned bias. */
    assert(drive_controller_update(&controller, &input, &right, 2000U) == before);
    assert(controller.integral_milli_pwm == integral);
    int16_t previous = before;
    uint32_t seed = 1U;
    const int16_t requests[] = {-1000, -700, -31, 0, 31, 600, 1000};
    for (uint32_t now = 2010U; now < 102010U; now += 10U) {
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        if ((now % 70U) == 0U) input.drive_mode = (uint8_t)(1U + (seed % 3U));
        if ((now % 110U) == 0U) input.throttle = requests[(seed >> 8) % 7U];
        right.speed_mm_s = (previous < 0) ? -700 : 700;
        const int16_t pwm = drive_controller_update(&controller, &input, &right, now);
        const int32_t magnitude = (pwm < 0) ? -(int32_t)pwm : pwm;
        const int32_t previous_magnitude = (previous < 0) ? -(int32_t)previous : previous;
        const int32_t change = magnitude - previous_magnitude;
        const int32_t fall = ((controller.state == DRIVE_CONTROL_BRAKING) ||
                               (controller.state == DRIVE_CONTROL_REVERSING)) ? 20 : 10;
        assert(change >= -fall && change <= 10);
        assert(!((previous > 0 && pwm < 0) || (previous < 0 && pwm > 0)));
        /* Repeated input changes must not bypass the 10 ms output cadence. */
        assert(drive_controller_update(&controller, &input, &right, now) == pwm);
        previous = pwm;
    }
}

static void test_stop_settle_starts_after_pwm_zero_and_reversal_can_cancel(void)
{
    drive_controller_t controller = {0};
    remote_command_t input = command(1000, 3U);
    wheel_speed_sample_t right = feedback(0);
    (void)drive_controller_update(&controller, &input, &right, 0U);
    for (uint32_t now = 10U; now <= 200U; now += 10U) {
        (void)drive_controller_update(&controller, &input, &right, now);
    }
    assert(controller.pwm == 200);
    input.throttle = -1000;
    assert(drive_controller_update(&controller, &input, &right, 210U) == 180);
    input.throttle = 1000;
    assert(drive_controller_update(&controller, &input, &right, 220U) == 190);

    input.throttle = -1000;
    /* Wheel is already stopped, but positive PWM must ramp all the way down. */
    for (uint32_t now = 230U; now <= 320U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) >= 0);
        assert(controller.state == DRIVE_CONTROL_BRAKING);
    }
    assert(controller.pwm == 0);
    input.throttle = 0;
    assert(drive_controller_update(&controller, &input, &right, 330U) == 0);
    input.throttle = -1000;
    assert(drive_controller_update(&controller, &input, &right, 340U) == 0);
    for (uint32_t now = 350U; now <= 440U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
    }
    assert(drive_controller_update(&controller, &input, &right, 450U) == -10);

    /* Normal ramps never delay invalid-feedback or remote-loss responses. */
    for (uint32_t now = 460U; now <= 1450U; now += 10U) {
        (void)drive_controller_update(&controller, &input, &right, now);
    }
    assert(controller.pwm < -400);
    right.state = WHEEL_SPEED_STALE;
    assert(drive_controller_update(&controller, &input, &right, 1451U) < -400);
    assert(controller.state == DRIVE_CONTROL_FEEDBACK_HOLD);
    assert(drive_controller_update(&controller, &input, &right, 1490U) == 0);
    assert(controller.state == DRIVE_CONTROL_FEEDBACK_ERROR);
}

static void test_rejection_of_load_disturbance_in_simple_plant(void)
{
    /* Synthetic first-order motor model, not a claim of measured tuning.
     * Nominal free speed 2800 mm/s, time constant about 120 ms. */
    drive_controller_t controller = {0};
    remote_command_t input = command(1000, 1U);
    int32_t velocity_milli = 0;
    uint32_t now = 0U;
    const uint8_t modes[] = {1U, 2U, 3U, 1U};
    for (size_t mode = 0U; mode < sizeof(modes); ++mode) {
        input.drive_mode = modes[mode];
        int32_t startup_peak = 0;
        for (uint32_t step = 0U; step < 1500U; ++step) {
            const wheel_speed_sample_t right = feedback(velocity_milli / 1000);
            const int16_t output = drive_controller_update(&controller, &input, &right, now);
            assert(output >= 0 && output <= 1000);
            const int32_t load = (step < 500U) ? 0 : 350;
            int32_t equilibrium = (int32_t)output * 2800 - load * 1000;
            if (equilibrium < 0) equilibrium = 0;
            velocity_milli += (equilibrium - velocity_milli) / 12;
            if ((mode == 0U) && (step < 500U) && (velocity_milli > startup_peak)) {
                startup_peak = velocity_milli;
            }
            now += 10U;
        }
        const int32_t actual = velocity_milli / 1000;
        /* Regression for the excessive initial feed-forward seen on the
         * raised vehicle. This model bounds startup, not road performance. */
        if (mode == 0U) assert(startup_peak <= 1200000);
        if (modes[mode] == 3U) {
            assert(controller.pwm == 1000 && actual > DRIVE_CONTROL_NOMINAL_MM_S);
        } else {
            const int32_t target = (modes[mode] == 1U) ? 1111 : 1666;
            assert(actual >= target - 5 && actual <= target + 5);
        }
    }
}

static void test_normal_release_then_stopped_neutral_permits_reverse(void)
{
    drive_controller_t controller = {0};
    remote_command_t input = command(1000, 3U);
    wheel_speed_sample_t right = feedback(500);
    (void)drive_controller_update(&controller, &input, &right, 0U);
    for (uint32_t now = 10U; now <= 1000U; now += 10U) {
        (void)drive_controller_update(&controller, &input, &right, now);
    }
    input.throttle = 0;
    right.speed_mm_s = 0;
    for (uint32_t now = 1010U; now <= 2000U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) ==
               (int16_t)(2000U - now)); /* Normal release: full range in 1 s. */
        assert(controller.reverse_stage == DRIVE_REVERSE_FORWARD_SEEN);
    }
    right.speed_mm_s = 51; /* PWM=0 alone does not prove stopped wheels. */
    for (uint32_t now = 2010U; now <= 2500U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
        assert(controller.reverse_stage == DRIVE_REVERSE_FORWARD_SEEN);
    }
    right.speed_mm_s = 50;
    for (uint32_t now = 2510U; now <= 2600U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
        assert(controller.reverse_stage == DRIVE_REVERSE_FORWARD_SEEN);
    }
    assert(drive_controller_update(&controller, &input, &right, 2610U) == 0);
    assert(controller.reverse_stage == DRIVE_REVERSE_READY);
    right.speed_mm_s = 0;
    for (uint32_t now = 2620U; now <= 4000U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
        assert(controller.reverse_stage == DRIVE_REVERSE_READY);
    }
    input.throttle = -1000;
    for (uint32_t now = 4010U; now <= 4110U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
        assert(controller.state != DRIVE_CONTROL_BRAKING);
    }
    assert(drive_controller_update(&controller, &input, &right, 4120U) == -10);
    assert(controller.reverse_stage == DRIVE_REVERSE_ACTIVE);
    input.throttle = 0;
    assert(drive_controller_update(&controller, &input, &right, 4130U) == 0);
    assert(controller.reverse_stage == DRIVE_REVERSE_ACTIVE);
    input.throttle = -1000;
    assert(drive_controller_update(&controller, &input, &right, 4140U) == 0);
    assert(drive_controller_update(&controller, &input, &right, 4150U) == -10);
}

static void test_short_neutral_then_reverse_still_brakes_until_second_pull(void)
{
    drive_controller_t controller = {0};
    remote_command_t input = command(1000, 3U);
    wheel_speed_sample_t right = feedback(0);
    (void)drive_controller_update(&controller, &input, &right, 0U);
    for (uint32_t now = 10U; now <= 100U; now += 10U) {
        (void)drive_controller_update(&controller, &input, &right, now);
    }
    input.throttle = 0;
    for (uint32_t now = 110U; now <= 150U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) > 0);
    }
    input.throttle = -1000;
    assert(drive_controller_update(&controller, &input, &right, 160U) == 30);
    for (uint32_t now = 170U; now <= 3000U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) >= 0);
        assert(controller.state == DRIVE_CONTROL_BRAKING);
        assert(controller.reverse_stage == DRIVE_REVERSE_WAIT_NEUTRAL);
    }
}

static void test_neutral_stop_settle_restarts_after_bad_feedback_or_motion(void)
{
    drive_controller_t controller = {0};
    remote_command_t input = command(1000, 3U);
    wheel_speed_sample_t right = feedback(0);
    (void)drive_controller_update(&controller, &input, &right, 0U);
    assert(drive_controller_update(&controller, &input, &right, 10U) == 10);
    input.throttle = 0;
    assert(drive_controller_update(&controller, &input, &right, 20U) == 0);
    for (uint32_t now = 30U; now <= 60U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
    }
    right.state = WHEEL_SPEED_STALE;
    assert(drive_controller_update(&controller, &input, &right, 70U) == 0);
    assert(controller.motion == DRIVE_MOTION_IDLE); /* Cancel neutral timer. */
    assert(drive_controller_update(&controller, &input, &right, 80U) == 0);
    right.state = WHEEL_SPEED_OK;
    for (uint32_t now = 90U; now <= 180U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
        assert(controller.reverse_stage == DRIVE_REVERSE_FORWARD_SEEN);
        assert(controller.motion == DRIVE_MOTION_NEUTRAL_SETTLE);
    }
    right.speed_mm_s = -51;
    assert(drive_controller_update(&controller, &input, &right, 190U) == 0);
    assert(controller.motion == DRIVE_MOTION_IDLE);
    right.speed_mm_s = -50;
    for (uint32_t now = 200U; now <= 290U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
        assert(controller.reverse_stage == DRIVE_REVERSE_FORWARD_SEEN);
    }
    assert(drive_controller_update(&controller, &input, &right, 300U) == 0);
    assert(controller.reverse_stage == DRIVE_REVERSE_READY);
    assert(controller.motion == DRIVE_MOTION_IDLE);
}

static void test_startup_and_faults_do_not_grant_reverse_from_idle_neutral(void)
{
    for (uint32_t fault = 0U; fault < 5U; ++fault) {
        drive_controller_t controller = {0};
        remote_command_t input = command(1000, 3U);
        wheel_speed_sample_t right = feedback(0);
        if (fault != 0U) {
            (void)drive_controller_update(&controller, &input, &right, 0U);
            for (uint32_t now = 10U; now <= 100U; now += 10U) {
                (void)drive_controller_update(&controller, &input, &right, now);
            }
            if (fault == 1U) {
                input.status = REMOTE_STATUS_ERROR;
                assert(drive_controller_update(&controller, &input, &right, 101U) == 0);
                input.status = REMOTE_STATUS_OK;
            } else if (fault == 2U) {
                right.state = WHEEL_SPEED_STALE;
                (void)drive_controller_update(&controller, &input, &right, 101U);
                assert(drive_controller_update(&controller, &input, &right, 140U) == 0);
                right.state = WHEEL_SPEED_OK;
            } else if (fault == 3U) {
                assert(drive_controller_update(&controller, &input, &right, 150U) == 0);
            } else drive_controller_stop(&controller);
        }
        input.throttle = 0;
        for (uint32_t now = 200U; now <= 2000U; now += 10U) {
            assert(drive_controller_update(&controller, &input, &right, now) == 0);
            assert(controller.reverse_stage == DRIVE_REVERSE_BRAKE_REQUIRED);
        }
        input.throttle = -1000;
        assert(drive_controller_update(&controller, &input, &right, 2010U) == 0);
        assert(controller.state == DRIVE_CONTROL_BRAKING);
    }
}

static void test_neutral_stop_permission_across_tick_wrap(void)
{
    drive_controller_t controller = {0};
    remote_command_t input = command(1000, 3U);
    wheel_speed_sample_t right = feedback(0);
    const uint32_t base = UINT32_MAX - 100U;
    assert(drive_controller_update(&controller, &input, &right, base) == 0);
    assert(drive_controller_update(&controller, &input, &right, base + 10U) == 10);
    input.throttle = 0;
    assert(drive_controller_update(&controller, &input, &right, base + 20U) == 0);
    for (uint32_t offset = 30U; offset <= 120U; offset += 10U) {
        assert(drive_controller_update(&controller, &input, &right, base + offset) == 0);
        assert(controller.reverse_stage == DRIVE_REVERSE_FORWARD_SEEN);
    }
    assert(drive_controller_update(&controller, &input, &right, base + 130U) == 0);
    assert(controller.reverse_stage == DRIVE_REVERSE_READY);
}

static void test_full_power_brake_is_faster_than_normal_release(void)
{
    drive_controller_t controller = {0};
    remote_command_t input = command(1000, 3U);
    wheel_speed_sample_t right = feedback(0);
    (void)drive_controller_update(&controller, &input, &right, 0U);
    for (uint32_t now = 10U; now <= 1000U; now += 10U) {
        (void)drive_controller_update(&controller, &input, &right, now);
    }
    input.throttle = -1000;
    assert(drive_controller_update(&controller, &input, &right, 1001U) == 1000);
    for (uint32_t now = 1010U; now <= 1500U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) ==
               (int16_t)(1000U - (now - 1000U) * 2U));
        assert(controller.state == DRIVE_CONTROL_BRAKING);
        assert(controller.integral_milli_pwm == 0);
    }
    for (uint32_t now = 1510U; now <= 2500U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
        assert(controller.reverse_stage == DRIVE_REVERSE_WAIT_NEUTRAL);
    }
    /* Returning to forward cancels braking and rearms the first reverse pull. */
    input.throttle = 1000;
    assert(drive_controller_update(&controller, &input, &right, 2510U) == 10);
    input.throttle = -1000;
    assert(drive_controller_update(&controller, &input, &right, 2520U) == 0);
    assert(controller.state == DRIVE_CONTROL_BRAKING);
}

static void test_faults_and_forward_input_rearm_reverse_brake(void)
{
    for (uint32_t fault = 0U; fault < 4U; ++fault) {
        drive_controller_t controller = {0};
        remote_command_t input = command(-1000, (uint8_t)(1U + fault % 3U));
        wheel_speed_sample_t right = feedback(0);
        prepare_reverse_from_rest(&controller, &input, &right);
        assert(drive_controller_update(&controller, &input, &right, 130U) == -10);
        if (fault == 0U) {
            input.status = REMOTE_STATUS_ERROR;
            assert(drive_controller_update(&controller, &input, &right, 131U) == 0);
        } else if (fault == 1U) {
            right.state = WHEEL_SPEED_STALE;
            assert(drive_controller_update(&controller, &input, &right, 131U) == -10);
            assert(drive_controller_update(&controller, &input, &right, 170U) == 0);
        } else if (fault == 2U) {
            assert(drive_controller_update(&controller, &input, &right, 180U) == 0);
        } else {
            drive_controller_stop(&controller); /* Service/timeout owner. */
        }
        assert(controller.reverse_stage == DRIVE_REVERSE_BRAKE_REQUIRED);
        input.status = REMOTE_STATUS_OK;
        right = feedback(0);
        for (uint32_t now = 190U; now <= 450U; now += 10U) {
            assert(drive_controller_update(&controller, &input, &right, now) == 0);
            assert(controller.state == DRIVE_CONTROL_BRAKING);
        }
        input.throttle = 0;
        assert(drive_controller_update(&controller, &input, &right, 460U) == 0);
        input.throttle = -1000;
        assert(drive_controller_update(&controller, &input, &right, 470U) == 0);
        for (uint32_t now = 480U; now <= 570U; now += 10U) {
            assert(drive_controller_update(&controller, &input, &right, now) == 0);
        }
        assert(drive_controller_update(&controller, &input, &right, 580U) == -10);
        input.throttle = 31; /* Any real forward command rearms, across modes. */
        (void)drive_controller_update(&controller, &input, &right, 590U);
        assert(controller.reverse_stage == DRIVE_REVERSE_BRAKE_REQUIRED);
        input.throttle = -1000;
        assert(drive_controller_update(&controller, &input, &right, 600U) == 0);
        assert(controller.state == DRIVE_CONTROL_BRAKING);
    }
}

static void test_forward_from_reverse_waits_until_pwm_zero_then_stopped(void)
{
    drive_controller_t controller = {0};
    remote_command_t input = command(-1000, 3U);
    wheel_speed_sample_t right = feedback(0);
    prepare_reverse_from_rest(&controller, &input, &right);
    for (uint32_t now = 130U; now <= 320U; now += 10U) {
        (void)drive_controller_update(&controller, &input, &right, now);
    }
    assert(controller.pwm == -200);
    input.throttle = 1000;
    for (uint32_t now = 330U; now <= 420U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) ==
               -(int16_t)(200U - (now - 320U) * 2U));
    }
    for (uint32_t now = 430U; now <= 520U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == 0);
    }
    assert(drive_controller_update(&controller, &input, &right, 530U) == 10);
}

static void test_brief_feedback_loss_ramps_and_recovers_without_rearming_reverse(void)
{
    drive_controller_t controller = {0};
    remote_command_t input = command(1000, 3U);
    wheel_speed_sample_t right = feedback(3000);
    (void)drive_controller_update(&controller, &input, &right, 0U);
    for (uint32_t now = 10U; now <= 1000U; now += 10U) {
        (void)drive_controller_update(&controller, &input, &right, now);
    }
    assert(controller.pwm == 1000);
    right.state = WHEEL_SPEED_STALE;
    assert(drive_controller_update(&controller, &input, &right, 1001U) == 1000);
    for (uint32_t now = 1010U; now <= 1030U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) == (int16_t)(2000U - now));
        assert(controller.state == DRIVE_CONTROL_FEEDBACK_HOLD);
        assert(controller.reverse_stage == DRIVE_REVERSE_FORWARD_SEEN);
    }
    right.state = WHEEL_SPEED_OK;
    assert(drive_controller_update(&controller, &input, &right, 1031U) == 970);
    assert(drive_controller_update(&controller, &input, &right, 1040U) == 980);
    input.throttle = -1000;
    right.state = WHEEL_SPEED_STALE;
    assert(drive_controller_update(&controller, &input, &right, 1041U) == 980);
    for (uint32_t now = 1050U; now <= 1070U; now += 10U) {
        assert(drive_controller_update(&controller, &input, &right, now) ==
               (int16_t)(980U - (now - 1040U) * 2U));
    }
    assert(drive_controller_update(&controller, &input, &right, 1080U) == 0);
    assert(controller.state == DRIVE_CONTROL_FEEDBACK_ERROR);
    right = feedback(0);
    assert(drive_controller_update(&controller, &input, &right, 1090U) == 0);
    input.throttle = 0;
    assert(drive_controller_update(&controller, &input, &right, 1100U) == 0);
    assert(controller.reverse_stage == DRIVE_REVERSE_READY);
    right.state = WHEEL_SPEED_STALE;
    assert(drive_controller_update(&controller, &input, &right, 1110U) == 0);
    assert(controller.reverse_stage == DRIVE_REVERSE_READY);
    right = feedback(0);
    input.throttle = -1000;
    assert(drive_controller_update(&controller, &input, &right, 1120U) == 0);
    assert(controller.reverse_stage == DRIVE_REVERSE_READY);
    assert(drive_controller_update(&controller, &input, &right, 1130U) == 0);
    assert(controller.reverse_stage == DRIVE_REVERSE_ACTIVE);
}

static void test_feedback_history_is_independent_of_diagnostic_state(void)
{
    drive_controller_t controller = {0};
    const remote_command_t input = command(1000, 3U);
    wheel_speed_sample_t right = feedback(0);
    right.state = WHEEL_SPEED_STALE;
    controller.state = DRIVE_CONTROL_TRACKING;
    assert(drive_controller_update(&controller, &input, &right, 0U) == 0);
    assert(!controller.feedback_seen && controller.state == DRIVE_CONTROL_FEEDBACK_ERROR);

    right.state = WHEEL_SPEED_OK;
    assert(drive_controller_update(&controller, &input, &right, 10U) == 0);
    assert(drive_controller_update(&controller, &input, &right, 20U) == 10);
    assert(controller.feedback_seen);
    /* A diagnostic label must neither create nor erase real feedback history. */
    controller.state = DRIVE_CONTROL_DISABLED;
    right.state = WHEEL_SPEED_STALE;
    assert(drive_controller_update(&controller, &input, &right, 21U) == 10);
    assert(drive_controller_update(&controller, &input, &right, 30U) == 0);
    assert(controller.feedback_seen && controller.state == DRIVE_CONTROL_FEEDBACK_HOLD);
    assert(drive_controller_update(&controller, &input, &right, 60U) == 0);
    assert(!controller.feedback_seen && controller.state == DRIVE_CONTROL_FEEDBACK_ERROR);
    controller.state = DRIVE_CONTROL_FULL;
    assert(drive_controller_update(&controller, &input, &right, 61U) == 0);
    assert(!controller.feedback_seen && controller.state == DRIVE_CONTROL_FEEDBACK_ERROR);
    right.state = WHEEL_SPEED_OK;
    assert(drive_controller_update(&controller, &input, &right, 70U) == 0);
    assert(controller.feedback_seen);
    assert(drive_controller_update(&controller, NULL, &right, 71U) == 0);
    assert(!controller.feedback_seen);
}

int main(void)
{
    test_mode_mapping_and_deadband();
    test_full_mode_and_downshift();
    test_reversal_and_opposite_startup_motion();
    test_invalid_feedback_input_and_timing();
    test_integrator_saturation_and_fractional_errors();
    test_mode_changes_preserve_integrator_and_slew_both_directions();
    test_stop_settle_starts_after_pwm_zero_and_reversal_can_cancel();
    test_rejection_of_load_disturbance_in_simple_plant();
    test_normal_release_then_stopped_neutral_permits_reverse();
    test_short_neutral_then_reverse_still_brakes_until_second_pull();
    test_neutral_stop_settle_restarts_after_bad_feedback_or_motion();
    test_startup_and_faults_do_not_grant_reverse_from_idle_neutral();
    test_neutral_stop_permission_across_tick_wrap();
    test_full_power_brake_is_faster_than_normal_release();
    test_faults_and_forward_input_rearm_reverse_brake();
    test_forward_from_reverse_waits_until_pwm_zero_then_stopped();
    test_brief_feedback_loss_ramps_and_recovers_without_rearming_reverse();
    test_feedback_history_is_independent_of_diagnostic_state();
    return 0;
}
