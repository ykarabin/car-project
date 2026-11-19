#include "vehicle_control.h"

#include <stddef.h>

#include "vehicle_log.h"
#include "drive_controller.h"
#include "drive_controller_config.h"
#include "motor_control.h"
#include "steering_calibration_service.h"
#include "steering_controller.h"
#include "steering_controller_config.h"
#include "board_controls.h"
#include "vehicle_input_config.h"
#include "control_watchdog.h"

#define CAL_JOG_PWM INT32_C(150)

_Static_assert(STEERING_CONTROL_RELEASE_SETTLE_MS > 0U &&
               STEERING_CONTROL_RELEASE_SETTLE_MS < STEERING_CONTROL_RELEASE_TIMEOUT_MS,
               "Bounded center-return interval");

typedef enum {
    STEERING_MODE_OFF,
    STEERING_MODE_MANUAL,
    STEERING_MODE_REMOTE,
    STEERING_MODE_CENTERING
} steering_mode_t;

static steering_calibration_service_t s_calibration;
static bool s_wait_neutral;
static steering_controller_t s_controller;
static drive_controller_t s_drive;
static steering_mode_t s_steering_mode;
static uint32_t s_centering_since;
static uint32_t s_centered_since;
static bool s_center_stable;

static bool neutral(const remote_command_t *command)
{
    return (command != NULL) && (command->steering >= -VEHICLE_STICK_DEADBAND) &&
           (command->steering <= VEHICLE_STICK_DEADBAND) &&
           (command->throttle >= -VEHICLE_STICK_DEADBAND) &&
           (command->throttle <= VEHICLE_STICK_DEADBAND);
}

static bool encoder_ready(const steering_encoder_sample_t *encoder)
{
    return (encoder != NULL) && (encoder->state == STEERING_ENCODER_OK) &&
           (encoder->raw < 4096U);
}

void vehicle_control_init(void)
{
    drive_controller_stop(&s_drive);
    steering_calibration_service_init(&s_calibration, HAL_GetTick());
    (void)steering_controller_init(&s_controller,
                                   s_calibration.has_saved ? &s_calibration.saved : NULL);
    s_wait_neutral = control_watchdog_was_reset();
    s_steering_mode = STEERING_MODE_OFF;
    s_centering_since = 0U;
    s_centered_since = 0U;
    s_center_stable = false;
    if (s_wait_neutral) {
        vehicle_log_text("WATCHDOG RESET | release both sticks to resume; outputs asleep");
    }
    steering_calibration_service_log_status(&s_calibration);
}

static const char *steering_state_name(void)
{
    if (s_wait_neutral) {
        return "WAIT_NEUTRAL";
    }
    switch (s_steering_mode) {
    case STEERING_MODE_MANUAL: return "MANUAL";
    case STEERING_MODE_CENTERING: return "CENTER";
    case STEERING_MODE_REMOTE: return s_controller.moving ? "MOVE" : "HOLD";
    case STEERING_MODE_OFF: return "OFF";
    default: return "OFF";
    }
}

void vehicle_control_log_status(const char *remote_state, uint8_t mode,
                                 const steering_encoder_sample_t *encoder,
                                 const wheel_speed_sample_t *right)
{
    /* Queue copied values; formatting stays in the logger. */
    const vehicle_log_message_t message = {
        .kind = VEHICLE_LOG_STATUS,
        .data.status = {
            .remote_state = remote_state,
            .drive_state = drive_controller_state_name(s_drive.state),
            .steering_state = steering_state_name(),
            .calibration = s_calibration.saved,
            .measured_mm_s = right != NULL ? right->speed_mm_s : 0,
            .target_mm_s = s_drive.target_mm_s,
            .drive_pwm = s_drive.pwm,
            .steering_pwm = s_controller.pwm,
            .raw = encoder != NULL ? encoder->raw : 0U,
            .drive_mode = mode,
            .cal_stage = s_calibration.stage,
            .position_valid = s_calibration.has_saved && encoder_ready(encoder),
            .speed_valid = right != NULL && right->state == WHEEL_SPEED_OK &&
                           right->age_ms < DRIVE_CONTROL_FEEDBACK_MAX_AGE_MS,
            .full_power = s_drive.full_power_requested
        }
    };
    (void)vehicle_log_publish(&message);
}

static int16_t release_steering(const char *reason)
{
    steering_controller_stop(&s_controller);
    s_steering_mode = STEERING_MODE_MANUAL;
    s_center_stable = false;
    (void)vehicle_log_publish(&(const vehicle_log_message_t){
        .kind = VEHICLE_LOG_STEER_RELEASE, .data.text = reason
    });
    return 0;
}

/* Return to center before handing remote steering back to manual control. */
static int16_t normal_steering(const remote_command_t *control,
                              const steering_encoder_sample_t *encoder, uint32_t now)
{
    const bool valid = s_calibration.has_saved && encoder_ready(encoder);
    const bool stick_neutral = (control->steering >= -VEHICLE_STICK_DEADBAND) &&
                               (control->steering <= VEHICLE_STICK_DEADBAND);
    if (!stick_neutral) {
        s_center_stable = false;
        const int16_t pwm = steering_controller_update(&s_controller, control->steering,
                                                       valid ? encoder->raw : 0U, valid, now);
        if (!s_controller.initialized) {
            /* Release remote ownership after a prolonged feedback fault. */
            if ((s_steering_mode == STEERING_MODE_REMOTE) ||
                (s_steering_mode == STEERING_MODE_CENTERING)) {
                return release_steering("FEEDBACK");
            }
            return 0;
        }
        s_steering_mode = STEERING_MODE_REMOTE;
        return pwm;
    }
    if (s_steering_mode == STEERING_MODE_REMOTE) {
        s_steering_mode = STEERING_MODE_CENTERING;
        s_centering_since = now;
        s_center_stable = false;
    }
    if (s_steering_mode == STEERING_MODE_CENTERING) {
        if ((now - s_centering_since) >= STEERING_CONTROL_RELEASE_TIMEOUT_MS) {
            return release_steering("TIMEOUT");
        }
        const int16_t pwm = steering_controller_update(&s_controller, 0,
                                                       valid ? encoder->raw : 0U, valid, now);
        if (!valid && !s_controller.initialized) {
            return release_steering("FEEDBACK");
        }
        /* Use the actual center position to decide when steering can be released. */
        const int32_t offset = valid ?
            steering_calibration_delta(s_calibration.saved.center, encoder->raw) : 0;
        if (valid && (pwm == 0) &&
            (offset > -STEERING_CONTROL_START_COUNTS) &&
            (offset < STEERING_CONTROL_START_COUNTS)) {
            if (!s_center_stable) {
                s_center_stable = true;
                s_centered_since = now;
            }
            if ((now - s_centered_since) >= STEERING_CONTROL_RELEASE_SETTLE_MS) {
                return release_steering("CENTERED");
            }
        } else {
            s_center_stable = false;
        }
        return pwm;
    }
    /* Remain manual until the next nonneutral steering command. */
    steering_controller_stop(&s_controller);
    s_steering_mode = STEERING_MODE_MANUAL;
    return 0;
}

/* Select neutral wait, calibration or normal control for this cycle. */
void vehicle_control_update(const remote_command_t *command,
                             const steering_encoder_sample_t *encoder,
                             const wheel_speed_sample_t *right_speed)
{
    const remote_command_t *control =
        (remote_command_valid(command) && (command->status == REMOTE_STATUS_OK)) ? command : NULL;
    const uint32_t now = HAL_GetTick();
    const steering_calibration_event_t event = steering_calibration_service_update(
        &s_calibration, neutral(control), encoder_ready(encoder),
        encoder != NULL ? encoder->raw : 0U, now);
    switch (event) {
    case STEERING_CAL_EVENT_STARTED:
        s_wait_neutral = false;
        break;
    case STEERING_CAL_EVENT_SAVED:
        (void)steering_controller_init(&s_controller, &s_calibration.saved);
        s_wait_neutral = true;
        break;
    case STEERING_CAL_EVENT_CANCELLED:
        s_wait_neutral = true;
        break;
    case STEERING_CAL_EVENT_NONE:
        break;
    }

    if (s_wait_neutral) {
        s_steering_mode = STEERING_MODE_OFF;
        steering_controller_stop(&s_controller);
        drive_controller_stop(&s_drive);
        motor_control_apply(NULL);
        if (neutral(control)) {
            s_wait_neutral = false;
        }
        return;
    }
    if (control == NULL) {
        s_steering_mode = STEERING_MODE_OFF;
        steering_controller_stop(&s_controller);
        drive_controller_stop(&s_drive);
        motor_control_apply(NULL);
    } else if (s_calibration.stage != BOARD_CAL_OFF) {
        s_steering_mode = STEERING_MODE_OFF;
        steering_controller_stop(&s_controller);
        drive_controller_stop(&s_drive);
        if (!encoder_ready(encoder)) {
            motor_control_apply(NULL);
        } else {
            const int32_t request = ((control->steering >= -VEHICLE_STICK_DEADBAND) &&
                                     (control->steering <= VEHICLE_STICK_DEADBAND)) ? 0 :
                                     ((int32_t)control->steering * CAL_JOG_PWM) / 1000;
            const motor_request_t output = {
                .steering_pwm = (int16_t)request,
                .steering_enabled = true
            };
            motor_control_apply(&output);
        }
    } else {
        motor_request_t output = {
            .rear_pwm = drive_controller_update(&s_drive, control, right_speed, now),
            .rear_enabled = true
        };
        output.steering_pwm = normal_steering(control, encoder, now);
        output.steering_enabled = (s_steering_mode == STEERING_MODE_REMOTE) ||
                                   (s_steering_mode == STEERING_MODE_CENTERING);
        motor_control_apply(&output);
    }
}
