#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "steering_controller.h"
#include "steering_controller_config.h"

static const steering_calibration_t s_real = {56U, 3654U, 548U};

static int32_t magnitude(int32_t value)
{
    return (value < 0) ? -value : value;
}

static uint16_t raw_for(const steering_controller_t *controller, int32_t position)
{
    int32_t raw = controller->calibration.center + (position * controller->direction);
    if (raw < 0) {
        raw += 4096;
    } else if (raw >= 4096) {
        raw -= 4096;
    }
    return (uint16_t)raw;
}

static void test_neutral_boundary(void)
{
    const int16_t requests[] = {-31, -30, -29, 0, 29, 30, 31};
    for (size_t i = 0U; i < sizeof(requests) / sizeof(requests[0]); ++i) {
        steering_controller_t controller;
        assert(steering_controller_init(&controller, &s_real));
        (void)steering_controller_update(&controller, requests[i], s_real.center, true, 0U);
        (void)steering_controller_update(&controller, requests[i], s_real.center, true, 10U);
        if (requests[i] >= -30 && requests[i] <= 30) {
            assert(controller.target_raw == s_real.center);
            assert(controller.pwm == 0);
        } else {
            assert(controller.target_raw != s_real.center);
            assert(requests[i] * controller.pwm > 0);
        }
    }
}

static void test_targets_and_unclamped_feedback(void)
{
    steering_controller_t controller;
    assert(steering_controller_init(&controller, &s_real));
    assert(controller.left_counts == -484 && controller.right_counts == 478);
    uint16_t raw;
    assert(steering_calibration_target(&s_real, 0, &raw) && raw == 56U);
    assert(steering_calibration_target(&s_real, -1000, &raw) && raw == 3668U);
    assert(steering_calibration_target(&s_real, 1000, &raw) && raw == 534U);
    assert(steering_calibration_target(&s_real, 500, &raw) && raw == 295U);
    assert(!steering_calibration_target(&s_real, 1001, &raw));
    assert(!steering_calibration_target(NULL, 0, &raw));

    /* Physical right endpoint is beyond the working target. A clamped
     * normalized measurement would report zero error and leave it there. */
    assert(steering_controller_update(&controller, 1000, 548U, true, 0U) == 0);
    assert(steering_controller_update(&controller, 1000, 548U, true, 10U) < 0);
    assert(steering_controller_update(&controller, 1000, 548U, true, 20U) < 0);
    assert(controller.target_raw == 534U);
}

static void test_period_ramps_and_limits(void)
{
    steering_controller_t controller;
    assert(steering_controller_init(&controller, &s_real));
    assert(steering_controller_update(&controller, 1000, 56U, true, 100U) == 0);
    for (uint32_t now = 101U; now < 110U; ++now) {
        assert(steering_controller_update(&controller, 1000, 56U, true, now) == 0);
        assert(!controller.moving && controller.error_counts == 0);
    }
    int16_t previous_pwm = 0;
    for (uint32_t now = 110U; now <= 1100U; now += 10U) {
        const int16_t pwm = steering_controller_update(&controller, 1000, 56U, true, now);
        assert(pwm >= 0 && pwm <= STEERING_CONTROL_PWM_MAX);
        assert(controller.error_counts == 478);
        assert((pwm - previous_pwm) <= STEERING_CONTROL_PWM_STEP);
        previous_pwm = pwm;
    }
    assert(previous_pwm == STEERING_CONTROL_PWM_MAX);
    /* Feedback reaches a limit between control updates: outward drive stops immediately. */
    assert(steering_controller_update(&controller, 1000, 534U, true, 1101U) == 0);
}

static void test_hysteresis_and_restart(void)
{
    steering_controller_t controller;
    assert(steering_controller_init(&controller, &s_real));
    assert(steering_controller_update(&controller, 0, 56U, true, 0U) == 0);
    assert(steering_controller_update(&controller, 0, 61U, true, 10U) == 0);
    assert(steering_controller_update(&controller, 0, 64U, true, 20U) < 0);
    assert(steering_controller_update(&controller, 0, 60U, true, 30U) == 0);
    assert(!controller.moving);
    assert(steering_controller_update(&controller, 0, 62U, true, 40U) == 0);
    assert(steering_controller_update(&controller, 0, 64U, true, 50U) < 0);
    assert(steering_controller_update(&controller, 0, 48U, true, 60U) ==
           0); /* Brake before sign reversal. */
    assert(steering_controller_update(&controller, 0, 48U, true, 70U) > 0);

    assert(steering_controller_update(&controller, 0, 48U, false, 71U) > 0);
    assert(controller.initialized);
    assert(steering_controller_update(&controller, 0, 48U, false, 110U) == 0);
    assert(!controller.initialized);
    assert(steering_controller_update(&controller, 1000, 300U, true, 111U) == 0);
    assert(controller.initialized && controller.error_counts == 0);
    assert(steering_controller_update(&controller, 1000, 300U, true, 200U) ==
           0); /* Long scheduling gap. */
    assert(controller.initialized && controller.error_counts == 0);
    assert(steering_controller_update(&controller, 1001, 300U, true, 210U) == 0);
    assert(steering_controller_update(&controller, 0, 4096U, true, 220U) == 0);
}

static void test_time_wrap_and_invalid_calibration(void)
{
    steering_controller_t controller;
    assert(!steering_controller_init(NULL, &s_real));
    assert(!steering_controller_init(&controller, NULL));
    assert(steering_controller_update(&controller, 0, 56U, true, 0U) == 0);
    assert(steering_controller_init(&controller, &s_real));
    const uint32_t start = UINT32_MAX - 5U;
    assert(steering_controller_update(&controller, 0, 548U, true, start) == 0);
    assert(steering_controller_update(&controller, 0, 548U, true, 4U) < 0);
    assert(controller.target_raw == s_real.center);
    assert(steering_controller_update(&controller, 0, 548U, true, 14U) < 0);
}

static void simulate(const steering_calibration_t *calibration, int32_t initial, int16_t request)
{
    steering_controller_t controller;
    assert(steering_controller_init(&controller, calibration));
    int32_t position_milli = initial * 1000;
    int32_t velocity_milli = 0;
    for (uint32_t now = 0U; now <= 7000U; now += 10U) {
        const int32_t position = position_milli / 1000;
        const uint16_t raw = raw_for(&controller, position);
        const int16_t pwm = steering_controller_update(&controller, request, raw, true, now);
        assert(magnitude(pwm) <= STEERING_CONTROL_PWM_MAX);
        /* Deliberately simple bench model: lag, breakaway threshold and
         * electrical brake. It checks convergence, not hardware tuning. */
        if (pwm == 0) {
            velocity_milli = 0;
        } else {
            const int32_t velocity_goal = (magnitude(pwm) < 40) ? 0 : (int32_t)pwm * 40;
            velocity_milli = ((velocity_milli * 3) + velocity_goal) / 4;
        }
        position_milli += velocity_milli;
        assert(position_milli >= -510000 && position_milli <= 510000);
    }
    const int32_t target = steering_calibration_delta(calibration->center, controller.target_raw) *
                           controller.direction;
    assert(magnitude((position_milli / 1000) - target) < STEERING_CONTROL_START_COUNTS);
    assert(controller.pwm == 0);
}

static void test_fast_step_response(void)
{
    steering_controller_t controller;
    assert(steering_controller_init(&controller, &s_real));
    assert(steering_controller_update(&controller, 0, 56U, true, 0U) == 0);
    assert(steering_controller_update(&controller, 1000, 56U, true, 1U) == 0);
    assert(steering_controller_update(&controller, 1000, 56U, true, 10U) == 200);
    assert(controller.target_raw == 534U); /* Full target on the first scheduled update. */
    assert(steering_controller_update(&controller, 1000, 56U, true, 20U) == 400);
    assert(steering_controller_update(&controller, 1000, 56U, true, 30U) == 600);
    assert(steering_controller_update(&controller, 1000, 56U, true, 40U) == 700);
    assert(steering_controller_update(&controller, -1000, 56U, true, 50U) == 0);
    assert(controller.target_raw == 3668U);
    assert(steering_controller_update(&controller, -1000, 56U, true, 60U) == -200);
    assert(steering_controller_update(&controller, 0, 56U, true, 70U) == 0);
}

static void test_limit_brakes_before_inward_reversal(void)
{
    steering_controller_t controller;
    assert(steering_controller_init(&controller, &s_real));
    (void)steering_controller_update(&controller, 1000, 56U, true, 0U);
    for (uint32_t now = 10U; now <= 40U; now += 10U) {
        (void)steering_controller_update(&controller, 1000, 56U, true, now);
    }
    assert(controller.pwm == 700);
    assert(steering_controller_update(&controller, 0, 548U, true, 50U) == 0);
    assert(steering_controller_update(&controller, 0, 548U, true, 51U) == 0);
    assert(steering_controller_update(&controller, 0, 548U, true, 60U) == -200);
}

static void test_brief_feedback_loss_only_reduces_output_then_recovers(void)
{
    steering_controller_t controller;
    assert(steering_controller_init(&controller, &s_real));
    (void)steering_controller_update(&controller, 1000, 56U, true, 0U);
    for (uint32_t now = 10U; now <= 40U; now += 10U) {
        (void)steering_controller_update(&controller, 1000, 56U, true, now);
    }
    assert(controller.pwm == 700);
    assert(steering_controller_update(&controller, -1000, 0U, false, 41U) == 700);
    assert(steering_controller_update(&controller, -1000, 0U, false, 50U) == 500);
    assert(steering_controller_update(&controller, -1000, 0U, false, 60U) == 300);
    assert(steering_controller_update(&controller, 1000, 56U, true, 61U) == 300);
    assert(steering_controller_update(&controller, 1000, 56U, true, 70U) == 500);
    assert(steering_controller_update(&controller, 1000, 0U, false, 80U) == 300);
    assert(steering_controller_update(&controller, 1000, 0U, false, 90U) == 100);
    assert(steering_controller_update(&controller, 1000, 0U, false, 100U) == 0);
    assert(controller.initialized);
    assert(steering_controller_update(&controller, 1000, 0U, false, 110U) == 0);
    assert(!controller.initialized);
}

int main(void)
{
    test_neutral_boundary();
    test_targets_and_unclamped_feedback();
    test_period_ramps_and_limits();
    test_hysteresis_and_restart();
    test_time_wrap_and_invalid_calibration();
    test_fast_step_response();
    test_limit_brakes_before_inward_reversal();
    test_brief_feedback_loss_only_reduces_output_then_recovers();
    simulate(&s_real, 492, 0);
    simulate(&s_real, -498, 0);
    simulate(&s_real, -498, 1000);
    simulate(&s_real, 492, -1000);
    simulate(&s_real, 0, 500);
    const steering_calibration_t reverse = {2000U, 2500U, 1500U};
    simulate(&reverse, 500, 0);
    simulate(&reverse, -500, 1000);
    return 0;
}
