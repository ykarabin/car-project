#include "steering_controller.h"

#include <stddef.h>

#include "int32_math.h"
#include "steering_controller_config.h"

_Static_assert(STEERING_CONTROL_PWM_MIN > 0, "Minimum drive must be positive");
_Static_assert(STEERING_CONTROL_PWM_MIN <= STEERING_CONTROL_PWM_MAX, "PWM range");
_Static_assert(STEERING_CONTROL_PWM_MAX <= 1000, "PWM bounds");
_Static_assert(STEERING_CONTROL_START_COUNTS > STEERING_CONTROL_STOP_COUNTS, "Hysteresis");
_Static_assert(STEERING_CONTROL_KP > 0, "Positive feedback gain");
_Static_assert(STEERING_CONTROL_PWM_STEP > 0, "Positive output rate");
_Static_assert(STEERING_CONTROL_MAX_GAP_MS > STEERING_CONTROL_PERIOD_MS, "Update time bounds");
_Static_assert(STEERING_CONTROL_FEEDBACK_GRACE_MS < STEERING_CONTROL_MAX_GAP_MS,
               "Feedback grace bound");

static int32_t coordinate(const steering_controller_t *controller, uint16_t raw)
{
    return steering_calibration_delta(controller->calibration.center, raw) * controller->direction;
}

static int32_t approach(int32_t value, int32_t target, int32_t step)
{
    if ((target - value) > step) {
        return value + step;
    }
    if ((value - target) > step) {
        return value - step;
    }
    return target;
}

bool steering_controller_init(steering_controller_t *controller,
                              const steering_calibration_t *calibration)
{
    if (controller == NULL) {
        return false;
    }
    *controller = (steering_controller_t){0};
    uint16_t left;
    uint16_t right;
    if (!steering_calibration_target(calibration, -1000, &left) ||
        !steering_calibration_target(calibration, 1000, &right)) {
        return false;
    }
    controller->calibration = *calibration;
    controller->direction =
        (steering_calibration_delta(calibration->center, calibration->right) > 0) ? 1 : -1;
    controller->left_counts = coordinate(controller, left);
    controller->right_counts = coordinate(controller, right);
    controller->target_raw = calibration->center;
    controller->calibrated = true;
    return true;
}

void steering_controller_stop(steering_controller_t *controller)
{
    if (controller != NULL) {
        controller->initialized = false;
        controller->moving = false;
        controller->pwm = 0;
        controller->error_counts = 0;
    }
}

int16_t steering_controller_update(steering_controller_t *controller, int16_t request, uint16_t raw,
                                   bool feedback_valid, uint32_t now_ms)
{
    if ((controller == NULL) || !controller->calibrated || (request < -1000) || (request > 1000)) {
        steering_controller_stop(controller);
        return 0;
    }
    const uint32_t elapsed = now_ms - controller->updated_at_ms;
    if (!feedback_valid || (raw >= 4096U)) {
        if (!controller->initialized ||
            ((now_ms - controller->feedback_at_ms) >= STEERING_CONTROL_FEEDBACK_GRACE_MS) ||
            (elapsed >= STEERING_CONTROL_MAX_GAP_MS)) {
            steering_controller_stop(controller);
            return 0;
        }
        /* Reduce PWM during feedback loss. */
        if (elapsed >= STEERING_CONTROL_PERIOD_MS) {
            const uint32_t step =
                ((uint32_t)STEERING_CONTROL_PWM_STEP * elapsed) / STEERING_CONTROL_PERIOD_MS;
            controller->pwm = (int16_t)approach(controller->pwm, 0, (int32_t)step);
            controller->updated_at_ms = now_ms;
        }
        return controller->pwm;
    }
    controller->feedback_at_ms = now_ms;
    const bool neutral =
        (request >= -VEHICLE_STICK_DEADBAND) && (request <= VEHICLE_STICK_DEADBAND);
    const int16_t target = neutral ? 0 : request;
    if (!steering_calibration_target(&controller->calibration, target, &controller->target_raw)) {
        steering_controller_stop(controller);
        return 0;
    }
    const int32_t actual = coordinate(controller, raw);
    const int32_t desired = coordinate(controller, controller->target_raw);
    if (!controller->initialized || (elapsed >= STEERING_CONTROL_MAX_GAP_MS)) {
        controller->updated_at_ms = now_ms;
        controller->error_counts = 0;
        controller->pwm = 0;
        controller->moving = false;
        controller->initialized = true;
        return 0;
    }
    /* Enforce travel limits on every call, including between control updates. */
    if (((actual <= controller->left_counts) && (controller->pwm < 0)) ||
        ((actual >= controller->right_counts) && (controller->pwm > 0))) {
        controller->pwm = 0;
        controller->updated_at_ms = now_ms;
        /* Hold zero PWM for one control period before reversing inward. */
        return 0;
    }
    if (elapsed < STEERING_CONTROL_PERIOD_MS) {
        return controller->pwm;
    }
    controller->updated_at_ms = now_ms;
    const int32_t error = desired - actual;
    controller->error_counts = error;
    const int32_t absolute_error = (int32_t)int32_magnitude(error);
    if ((absolute_error <= STEERING_CONTROL_STOP_COUNTS) ||
        (!controller->moving && (absolute_error < STEERING_CONTROL_START_COUNTS))) {
        controller->pwm = 0;
        controller->moving = false;
        return 0;
    }
    controller->moving = true;
    int32_t drive = absolute_error * STEERING_CONTROL_KP;
    if (drive < STEERING_CONTROL_PWM_MIN) {
        drive = STEERING_CONTROL_PWM_MIN;
    }
    if (drive > STEERING_CONTROL_PWM_MAX) {
        drive = STEERING_CONTROL_PWM_MAX;
    }
    if (error < 0) {
        drive = -drive;
    }
    if (((actual <= controller->left_counts) && (drive < 0)) ||
        ((actual >= controller->right_counts) && (drive > 0))) {
        drive = 0;
    }

    /* Stop before reversing; position limits bypass the PWM ramp. */
    if ((drive == 0) || ((controller->pwm < 0) && (drive > 0)) ||
        ((controller->pwm > 0) && (drive < 0))) {
        controller->pwm = 0;
    } else {
        controller->pwm = (int16_t)approach(controller->pwm, drive, STEERING_CONTROL_PWM_STEP);
    }
    return controller->pwm;
}
