#include "drive_controller.h"

#include <stddef.h>

#include "drive_controller_config.h"

_Static_assert(DRIVE_CONTROL_MODE1_MAX_MM_S > 0, "Positive mode-1 speed");
_Static_assert(DRIVE_CONTROL_MODE2_MAX_MM_S > DRIVE_CONTROL_MODE1_MAX_MM_S, "Mode limits");
_Static_assert(DRIVE_CONTROL_NOMINAL_MM_S >= DRIVE_CONTROL_MODE2_MAX_MM_S, "Nominal scale");
_Static_assert(DRIVE_CONTROL_NOMINAL_MM_S <= DRIVE_CONTROL_FEEDBACK_MAX_MM_S, "Feedback range");
_Static_assert(DRIVE_CONTROL_PERIOD_MS > 0U, "Control period");
_Static_assert(DRIVE_CONTROL_MAX_GAP_MS > DRIVE_CONTROL_PERIOD_MS, "Control gap");
_Static_assert(DRIVE_CONTROL_PWM_MAX == 1000, "Full bridge duty");
_Static_assert(DRIVE_CONTROL_PWM_RISE_STEP > 0, "Positive PWM rise rate");
_Static_assert(DRIVE_CONTROL_PWM_FALL_STEP > 0, "Positive PWM fall rate");
_Static_assert(DRIVE_CONTROL_PWM_BRAKE_STEP >= DRIVE_CONTROL_PWM_FALL_STEP, "Brake PWM rate");
_Static_assert(DRIVE_CONTROL_FEEDBACK_GRACE_MS < DRIVE_CONTROL_MAX_GAP_MS, "Feedback grace bound");
_Static_assert(DRIVE_CONTROL_FEED_FORWARD_PWM > 0 &&
                   DRIVE_CONTROL_FEED_FORWARD_PWM <= DRIVE_CONTROL_PWM_MAX,
               "Feed-forward duty");
_Static_assert(VEHICLE_STICK_DEADBAND >= 0, "Stick deadband");
_Static_assert(DRIVE_CONTROL_FULL_STICK > VEHICLE_STICK_DEADBAND, "Full stick");
_Static_assert(DRIVE_CONTROL_FULL_STICK <= 1000, "Stick bounds");
_Static_assert(DRIVE_CONTROL_KP > 0 && DRIVE_CONTROL_KI > 0, "Positive PI gains");

static int32_t clamp(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

void drive_controller_stop(drive_controller_t *controller)
{
    if (controller != NULL) {
        *controller = (drive_controller_t){0};
    }
}

static bool motion_initialized(const drive_controller_t *controller)
{
    /* Running and reversal phases already have control timing. */
    return (controller->motion == DRIVE_MOTION_RUNNING) ||
           (controller->motion == DRIVE_MOTION_REVERSING) ||
           (controller->motion == DRIVE_MOTION_REVERSE_SETTLE);
}

static bool motion_reversing(const drive_controller_t *controller)
{
    return (controller->motion == DRIVE_MOTION_REVERSING) ||
           (controller->motion == DRIVE_MOTION_REVERSE_SETTLE);
}

static bool motion_settling(const drive_controller_t *controller)
{
    return (controller->motion == DRIVE_MOTION_REVERSE_SETTLE) ||
           (controller->motion == DRIVE_MOTION_NEUTRAL_SETTLE);
}

static void cancel_settle(drive_controller_t *controller)
{
    if (controller->motion == DRIVE_MOTION_REVERSE_SETTLE) {
        controller->motion = DRIVE_MOTION_REVERSING;
    } else if (controller->motion == DRIVE_MOTION_NEUTRAL_SETTLE) {
        controller->motion = DRIVE_MOTION_IDLE;
    }
}

static bool feedback_ready(const wheel_speed_sample_t *right)
{
    return (right != NULL) && (right->state == WHEEL_SPEED_OK) &&
           (right->age_ms < DRIVE_CONTROL_FEEDBACK_MAX_AGE_MS) && (right->interval_ms > 0U) &&
           (right->interval_ms < REAR_ENCODER_RIGHT_MAX_GAP_MS) &&
           (right->speed_mm_s >= -DRIVE_CONTROL_FEEDBACK_MAX_MM_S) &&
           (right->speed_mm_s <= DRIVE_CONTROL_FEEDBACK_MAX_MM_S);
}

static void brake(drive_controller_t *controller, drive_control_state_t state, uint32_t now_ms)
{
    controller->pwm = 0;
    controller->integral_milli_pwm = 0;
    controller->updated_at_ms = now_ms;
    controller->state = state;
}

static int16_t ramp_to_zero(drive_controller_t *controller, drive_control_state_t state,
                            uint32_t now_ms, uint32_t elapsed_ms, uint32_t fall_step)
{
    controller->state = state;
    if (state != DRIVE_CONTROL_FEEDBACK_HOLD) {
        controller->integral_milli_pwm = 0;
    }
    if (elapsed_ms >= DRIVE_CONTROL_PERIOD_MS) {
        const int32_t previous =
            (controller->pwm < 0) ? -(int32_t)controller->pwm : controller->pwm;
        const int32_t fall = (int32_t)((fall_step * elapsed_ms) / DRIVE_CONTROL_PERIOD_MS);
        const int32_t output = clamp(previous - fall, 0, DRIVE_CONTROL_PWM_MAX);
        controller->pwm = (int16_t)((controller->pwm < 0) ? -output : output);
        controller->updated_at_ms = now_ms;
    }
    if ((controller->pwm == 0) && (state == DRIVE_CONTROL_STOPPING)) {
        controller->state = DRIVE_CONTROL_STOP;
        controller->motion = DRIVE_MOTION_IDLE;
    }
    return controller->pwm;
}

static int32_t requested_speed(const remote_command_t *command)
{
    int32_t request = command->throttle;
    const int32_t sign = (request < 0) ? -1 : 1;
    if (request < 0) {
        request = -request;
    }
    if (request <= VEHICLE_STICK_DEADBAND) {
        return 0;
    }
    const int32_t maximum = (command->drive_mode == 1U) ?
                                DRIVE_CONTROL_MODE1_MAX_MM_S :
                                ((command->drive_mode == 2U) ? DRIVE_CONTROL_MODE2_MAX_MM_S :
                                                               DRIVE_CONTROL_NOMINAL_MM_S);
    return sign * ((request - VEHICLE_STICK_DEADBAND) * maximum / (1000 - VEHICLE_STICK_DEADBAND));
}

/* First reverse pull brakes; neutral and a second pull permit reverse. */
static bool update_reverse_gate(drive_controller_t *controller, int16_t throttle)
{
    if (throttle > VEHICLE_STICK_DEADBAND) {
        if (controller->reverse_stage != DRIVE_REVERSE_FORWARD_SEEN) {
            controller->reverse_stage = DRIVE_REVERSE_BRAKE_REQUIRED;
        }
    } else if (throttle >= -VEHICLE_STICK_DEADBAND) {
        if (controller->reverse_stage == DRIVE_REVERSE_WAIT_NEUTRAL) {
            controller->reverse_stage = DRIVE_REVERSE_READY;
        }
    } else {
        if ((controller->reverse_stage == DRIVE_REVERSE_BRAKE_REQUIRED) ||
            (controller->reverse_stage == DRIVE_REVERSE_FORWARD_SEEN)) {
            controller->reverse_stage = DRIVE_REVERSE_WAIT_NEUTRAL;
        }
        return controller->reverse_stage == DRIVE_REVERSE_WAIT_NEUTRAL;
    }
    return false;
}

static void update_neutral_stop(drive_controller_t *controller, uint32_t now_ms)
{
    /* A settled neutral stop grants reverse only after forward drive. */
    if ((controller->reverse_stage != DRIVE_REVERSE_FORWARD_SEEN) ||
        (controller->measured_mm_s < -DRIVE_CONTROL_REVERSE_STOP_MM_S) ||
        (controller->measured_mm_s > DRIVE_CONTROL_REVERSE_STOP_MM_S)) {
        controller->motion = DRIVE_MOTION_IDLE;
        return;
    }
    if (controller->motion != DRIVE_MOTION_NEUTRAL_SETTLE) {
        controller->motion = DRIVE_MOTION_NEUTRAL_SETTLE;
        controller->stopped_since_ms = now_ms;
    }
    if ((now_ms - controller->stopped_since_ms) >= DRIVE_CONTROL_REVERSE_SETTLE_MS) {
        controller->reverse_stage = DRIVE_REVERSE_READY;
        controller->motion = DRIVE_MOTION_IDLE;
    }
}

static int32_t regulate(drive_controller_t *controller, int32_t target, int32_t error,
                        int32_t fall_limit, int32_t rise_limit, uint32_t elapsed_ms)
{
    const int32_t feed_forward =
        target * DRIVE_CONTROL_FEED_FORWARD_PWM / DRIVE_CONTROL_NOMINAL_MM_S;
    const int32_t proportional = error * DRIVE_CONTROL_KP / 1000;
    /* Preserve fractional PWM so small errors accumulate. */
    const int64_t increment = (int64_t)error * DRIVE_CONTROL_KI * elapsed_ms / 1000;
    const int64_t candidate = (int64_t)controller->integral_milli_pwm + increment;
    const int32_t integral_limit = DRIVE_CONTROL_PWM_MAX * 1000;
    int32_t integral;
    if (candidate > integral_limit) {
        integral = integral_limit;
    } else if (candidate < -integral_limit) {
        integral = -integral_limit;
    } else {
        integral = (int32_t)candidate;
    }
    const int32_t raw = feed_forward + proportional + controller->integral_milli_pwm / 1000;
    /* Anti-windup: integrate only when the error does not push farther into a limit. */
    if (!(((error > 0) && (raw >= rise_limit)) || ((error < 0) && (raw <= fall_limit)))) {
        controller->integral_milli_pwm = integral;
    }
    return clamp(feed_forward + proportional + controller->integral_milli_pwm / 1000, fall_limit,
                 rise_limit);
}

static int16_t handle_feedback_loss(drive_controller_t *controller, const remote_command_t *command,
                                    uint32_t now_ms)
{
    const uint32_t elapsed = now_ms - controller->updated_at_ms;
    if (controller->feedback_seen &&
        ((now_ms - controller->feedback_at_ms) < DRIVE_CONTROL_FEEDBACK_GRACE_MS) &&
        (elapsed < DRIVE_CONTROL_MAX_GAP_MS)) {
        const bool braking = update_reverse_gate(controller, command->throttle);
        cancel_settle(controller);
        if (!motion_initialized(controller)) {
            controller->state = DRIVE_CONTROL_FEEDBACK_HOLD;
            return 0;
        }
        /* Stale feedback permits deceleration only. */
        return ramp_to_zero(controller, DRIVE_CONTROL_FEEDBACK_HOLD, now_ms, elapsed,
                            braking ? DRIVE_CONTROL_PWM_BRAKE_STEP : DRIVE_CONTROL_PWM_FALL_STEP);
    }
    brake(controller, DRIVE_CONTROL_FEEDBACK_ERROR, now_ms);
    controller->feedback_seen = false;
    controller->motion = DRIVE_MOTION_IDLE;
    controller->reverse_stage = DRIVE_REVERSE_BRAKE_REQUIRED;
    controller->measured_mm_s = 0;
    controller->error_mm_s = 0;
    return 0;
}

/* Require zero PWM and a settled wheel before reversing. */
static bool settle_reversal(drive_controller_t *controller, uint32_t now_ms, uint32_t elapsed)
{
    if (motion_reversing(controller)) {
        if (controller->pwm != 0) {
            controller->motion = DRIVE_MOTION_REVERSING;
            (void)ramp_to_zero(controller, DRIVE_CONTROL_REVERSING, now_ms, elapsed,
                               DRIVE_CONTROL_PWM_BRAKE_STEP);
            return false;
        }
        if ((controller->measured_mm_s >= -DRIVE_CONTROL_REVERSE_STOP_MM_S) &&
            (controller->measured_mm_s <= DRIVE_CONTROL_REVERSE_STOP_MM_S)) {
            if (controller->motion != DRIVE_MOTION_REVERSE_SETTLE) {
                controller->motion = DRIVE_MOTION_REVERSE_SETTLE;
                controller->stopped_since_ms = now_ms;
            }
        } else {
            controller->motion = DRIVE_MOTION_REVERSING;
        }
        if ((controller->motion != DRIVE_MOTION_REVERSE_SETTLE) ||
            ((now_ms - controller->stopped_since_ms) < DRIVE_CONTROL_REVERSE_SETTLE_MS)) {
            brake(controller, DRIVE_CONTROL_REVERSING, now_ms);
            return false;
        }
    } else {
        controller->motion = DRIVE_MOTION_RUNNING;
    }
    return true;
}

static int16_t update_pwm(drive_controller_t *controller, int32_t direction, uint32_t now_ms,
                          uint32_t elapsed)
{
    if (elapsed < DRIVE_CONTROL_PERIOD_MS) {
        return controller->pwm;
    }
    controller->updated_at_ms = now_ms;
    controller->direction = direction;
    controller->motion = DRIVE_MOTION_RUNNING;
    const int32_t previous = (controller->pwm < 0) ? -(int32_t)controller->pwm : controller->pwm;
    const int32_t rise =
        (int32_t)((DRIVE_CONTROL_PWM_RISE_STEP * elapsed) / DRIVE_CONTROL_PERIOD_MS);
    const int32_t fall =
        (int32_t)((DRIVE_CONTROL_PWM_FALL_STEP * elapsed) / DRIVE_CONTROL_PERIOD_MS);
    const int32_t fall_limit = clamp(previous - fall, 0, DRIVE_CONTROL_PWM_MAX);
    const int32_t rise_limit = clamp(previous + rise, 0, DRIVE_CONTROL_PWM_MAX);
    int32_t output;
    if (controller->full_power_requested) {
        controller->integral_milli_pwm = 0;
        output = rise_limit;
        controller->state = DRIVE_CONTROL_FULL;
    } else {
        output = regulate(controller, direction * controller->target_mm_s,
                          direction * controller->error_mm_s, fall_limit, rise_limit, elapsed);
        controller->state = DRIVE_CONTROL_TRACKING;
    }
    controller->pwm = (int16_t)(direction * output);
    if (controller->pwm > 0) {
        controller->reverse_stage = DRIVE_REVERSE_FORWARD_SEEN;
    }
    return controller->pwm;
}

/* Apply brake gestures and direction checks before updating PWM. */
static int16_t update_motion(drive_controller_t *controller, const remote_command_t *command,
                             int32_t previous_target, uint32_t now_ms, uint32_t elapsed)
{
    if (update_reverse_gate(controller, command->throttle)) {
        /* Keep bridges enabled while braking; holding reverse never grants permission. */
        const bool initialized = motion_initialized(controller);
        controller->motion = DRIVE_MOTION_RUNNING;
        if (!initialized) {
            brake(controller, DRIVE_CONTROL_BRAKING, now_ms);
            return 0;
        }
        return ramp_to_zero(controller, DRIVE_CONTROL_BRAKING, now_ms, elapsed,
                            DRIVE_CONTROL_PWM_BRAKE_STEP);
    }
    if ((controller->target_mm_s == 0) && (controller->pwm == 0)) {
        brake(controller, DRIVE_CONTROL_STOP, now_ms);
        controller->motion =
            motion_settling(controller) ? DRIVE_MOTION_NEUTRAL_SETTLE : DRIVE_MOTION_IDLE;
        update_neutral_stop(controller, now_ms);
        return 0;
    }
    if (!motion_initialized(controller)) {
        brake(controller, DRIVE_CONTROL_WAITING, now_ms);
        controller->motion = DRIVE_MOTION_RUNNING;
        return 0;
    }
    if (controller->target_mm_s == 0) {
        controller->motion = DRIVE_MOTION_RUNNING;
        return ramp_to_zero(controller, DRIVE_CONTROL_STOPPING, now_ms, elapsed,
                            DRIVE_CONTROL_PWM_FALL_STEP);
    }
    if ((controller->target_mm_s < 0) && (controller->reverse_stage == DRIVE_REVERSE_READY)) {
        controller->reverse_stage = DRIVE_REVERSE_ACTIVE;
        controller->motion = DRIVE_MOTION_REVERSING;
    }
    const int32_t direction = (controller->target_mm_s < 0) ? -1 : 1;
    const bool opposite_motion =
        direction * controller->measured_mm_s < -DRIVE_CONTROL_REVERSE_STOP_MM_S;
    /* Returning to the old command direction can cancel a reversal; actual opposite motion cannot.
     */
    if (motion_reversing(controller) && (direction == controller->direction) &&
        (direction * previous_target < 0) && !opposite_motion) {
        controller->motion = DRIVE_MOTION_RUNNING;
    }
    if (((controller->direction != 0) && (direction != controller->direction)) || opposite_motion) {
        if (!motion_reversing(controller)) {
            controller->motion = DRIVE_MOTION_REVERSING;
        }
    }
    if (!settle_reversal(controller, now_ms, elapsed)) {
        return controller->pwm;
    }
    return update_pwm(controller, direction, now_ms, elapsed);
}

int16_t drive_controller_update(drive_controller_t *controller, const remote_command_t *command,
                                const wheel_speed_sample_t *right, uint32_t now_ms)
{
    if (controller == NULL) {
        return 0;
    }
    if (!remote_command_valid(command) || (command->status != REMOTE_STATUS_OK)) {
        drive_controller_stop(controller);
        return 0;
    }
    const int32_t previous_target = controller->target_mm_s;
    controller->target_mm_s = requested_speed(command);
    controller->full_power_requested =
        (command->drive_mode == 3U) && ((command->throttle >= DRIVE_CONTROL_FULL_STICK) ||
                                        (command->throttle <= -DRIVE_CONTROL_FULL_STICK));
    if (!feedback_ready(right)) {
        return handle_feedback_loss(controller, command, now_ms);
    }

    controller->feedback_at_ms = now_ms;
    controller->feedback_seen = true;
    controller->measured_mm_s = right->speed_mm_s;
    controller->error_mm_s = controller->target_mm_s - controller->measured_mm_s;
    const uint32_t elapsed = now_ms - controller->updated_at_ms;
    if (elapsed >= DRIVE_CONTROL_MAX_GAP_MS) {
        controller->reverse_stage = DRIVE_REVERSE_BRAKE_REQUIRED;
        if (motion_initialized(controller)) {
            brake(controller, DRIVE_CONTROL_WAITING, now_ms);
            controller->motion = DRIVE_MOTION_RUNNING;
            return 0;
        }
    }
    return update_motion(controller, command, previous_target, now_ms, elapsed);
}

const char *drive_controller_state_name(drive_control_state_t state)
{
    switch (state) {
    case DRIVE_CONTROL_DISABLED: return "DISABLED";
    case DRIVE_CONTROL_WAITING: return "WAITING";
    case DRIVE_CONTROL_STOP: return "STOP";
    case DRIVE_CONTROL_STOPPING: return "STOPPING";
    case DRIVE_CONTROL_TRACKING: return "TRACK";
    case DRIVE_CONTROL_FULL: return "FULL";
    case DRIVE_CONTROL_BRAKING: return "BRAKING";
    case DRIVE_CONTROL_REVERSING: return "REVERSING";
    case DRIVE_CONTROL_FEEDBACK_HOLD: return "FEEDBACK_HOLD";
    case DRIVE_CONTROL_FEEDBACK_ERROR: return "FEEDBACK_ERROR";
    default: return "?";
    }
}
