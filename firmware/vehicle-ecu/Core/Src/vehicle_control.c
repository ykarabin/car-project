#include "vehicle_control.h"

#include <stddef.h>

#include "vehicle_log.h"
#include "drive_controller.h"
#include "drive_controller_config.h"
#include "motor_control.h"
#include "steering_calibration.h"
#include "steering_calibration_storage.h"
#include "steering_controller.h"
#include "steering_controller_config.h"
#include "board_controls.h"
#include "vehicle_input_config.h"
#include "control_watchdog.h"

#define CAL_JOG_PWM INT32_C(150)
#define CAL_SETTLE_MS UINT32_C(100)

_Static_assert(STEERING_CONTROL_RELEASE_SETTLE_MS > 0U &&
               STEERING_CONTROL_RELEASE_SETTLE_MS < STEERING_CONTROL_RELEASE_TIMEOUT_MS,
               "Bounded center-return interval");

typedef enum {
    STEERING_MODE_OFF,
    STEERING_MODE_MANUAL,
    STEERING_MODE_REMOTE,
    STEERING_MODE_CENTERING
} steering_mode_t;

static steering_calibration_t s_saved;
static steering_calibration_t s_draft;
static bool s_has_saved;
static board_cal_stage_t s_cal_stage;
static bool s_wait_neutral;
static bool s_stable;
static uint16_t s_stable_raw;
static uint32_t s_stable_since;
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
    s_saved = (steering_calibration_t){0};
    s_draft = (steering_calibration_t){0};
    s_has_saved = steering_calibration_storage_load(&s_saved);
    if (!steering_controller_init(&s_controller, s_has_saved ? &s_saved : NULL)) {
        s_has_saved = false;
    }
    s_cal_stage = BOARD_CAL_OFF;
    s_wait_neutral = control_watchdog_was_reset();
    s_stable = false;
    s_steering_mode = STEERING_MODE_OFF;
    s_centering_since = 0U;
    s_centered_since = 0U;
    s_center_stable = false;
    board_controls_init(HAL_GetTick());
    if (s_wait_neutral) {
        vehicle_log_text("WATCHDOG RESET | release both sticks to resume; outputs asleep");
    }
    (void)vehicle_log_publish(&(const vehicle_log_message_t){
        .kind = VEHICLE_LOG_CAL_STATUS,
        .data.cal_status = {.calibration = s_saved, .saved = s_has_saved,
                            .flash_available = steering_calibration_storage_available()}
    });
    if (!s_has_saved) {
        vehicle_log_text("CAL REQUIRED | hold PC13 for calibration; rear drive available normally");
    }
}

static void update_stability(const remote_command_t *command,
                             const steering_encoder_sample_t *encoder, uint32_t now)
{
    if ((s_cal_stage == BOARD_CAL_OFF) || !neutral(command) || !encoder_ready(encoder)) {
        s_stable = false;
        return;
    }
    const int32_t change = steering_calibration_delta(s_stable_raw, encoder->raw);
    if (!s_stable || (change < -3) || (change > 3)) {
        s_stable_raw = encoder->raw;
        s_stable_since = now;
        s_stable = true;
    }
}

static bool capture_ready(const remote_command_t *command,
                          const steering_encoder_sample_t *encoder, uint32_t now)
{
    if (!neutral(command) || !encoder_ready(encoder) || !s_stable ||
        ((now - s_stable_since) < CAL_SETTLE_MS)) {
        vehicle_log_text("CAL ERR | release both sticks; need fresh, steady encoder for 100 ms");
        return false;
    }
    return true;
}

static void set_cal_stage(board_cal_stage_t stage, uint32_t now)
{
    s_cal_stage = stage;
    board_controls_set_stage(stage, now);
}

static void calibration_error(uint32_t now)
{
    board_controls_signal(BOARD_NOTICE_ERROR, now);
}

static void start_calibration(const remote_command_t *command,
                              const steering_encoder_sample_t *encoder, uint32_t now)
{
    if (!neutral(command) || !encoder_ready(encoder)) {
        vehicle_log_text("CAL ERR | calibration needs valid remote, neutral sticks and encoder OK");
        calibration_error(now);
        return;
    }
    motor_control_apply(NULL);
    s_draft = (steering_calibration_t){0};
    s_wait_neutral = false;
    s_stable = false;
    set_cal_stage(BOARD_CAL_CENTER, now);
    vehicle_log_text("CAL STARTED | rear asleep, steering PWM limited to 15%; set wheels straight then confirm center");
}

static void cancel_calibration(uint32_t now)
{
    motor_control_apply(NULL);
    s_wait_neutral = true;
    set_cal_stage(BOARD_CAL_OFF, now);
    vehicle_log_text("CAL CANCELLED | saved calibration retained; release both sticks to resume");
}

/* Called only after the button handler accepts a steady right point and
 * validates the complete draft. No other calibration entry point exists. */
static void save_calibration(uint32_t now)
{
    /* All bridges must be asleep before synchronous Flash erase/program. */
    motor_control_apply(NULL);
    if (!steering_calibration_storage_save(&s_draft)) {
        vehicle_log_text("CAL ERR | Flash save failed; previous calibration retained, service remains active");
        calibration_error(now);
        return;
    }
    s_saved = s_draft;
    s_has_saved = true;
    (void)steering_controller_init(&s_controller, &s_saved);
    s_wait_neutral = true;
    set_cal_stage(BOARD_CAL_OFF, now);
    board_controls_signal(BOARD_NOTICE_SAVED, now);
    (void)vehicle_log_publish(&(const vehicle_log_message_t){
        .kind = VEHICLE_LOG_CAL_SAVED, .data.calibration = s_saved
    });
}

static void process_button(const remote_command_t *command,
                            const steering_encoder_sample_t *encoder, uint32_t now)
{
    const board_button_event_t event = board_controls_poll(now);
    if (event == BOARD_BUTTON_LONG) {
        if (s_cal_stage != BOARD_CAL_OFF) {
            cancel_calibration(now);
        } else {
            start_calibration(command, encoder, now);
        }
    } else if ((event == BOARD_BUTTON_SHORT) && (s_cal_stage != BOARD_CAL_OFF)) {
        if (!capture_ready(command, encoder, now)) {
            calibration_error(now);
            return;
        }
        if (s_cal_stage == BOARD_CAL_CENTER) {
            s_draft = (steering_calibration_t){.center = encoder->raw};
            (void)vehicle_log_publish(&(const vehicle_log_message_t){
                .kind = VEHICLE_LOG_CAL_CENTER, .data.raw = s_draft.center
            });
            set_cal_stage(BOARD_CAL_LEFT, now);
        } else if (s_cal_stage == BOARD_CAL_LEFT) {
            const int32_t delta = steering_calibration_delta(s_draft.center, encoder->raw);
            if ((delta > -STEERING_CAL_MIN_SIDE_COUNTS) && (delta < STEERING_CAL_MIN_SIDE_COUNTS)) {
                vehicle_log_text("CAL ERR | left endpoint too close to center; step retained");
                calibration_error(now);
                return;
            }
            s_draft.left = encoder->raw;
            (void)vehicle_log_publish(&(const vehicle_log_message_t){
                .kind = VEHICLE_LOG_CAL_LEFT, .data.raw = s_draft.left
            });
            set_cal_stage(BOARD_CAL_RIGHT, now);
        } else if (s_cal_stage == BOARD_CAL_RIGHT) {
            steering_calibration_t candidate = s_draft;
            candidate.right = encoder->raw;
            if (!steering_calibration_valid(&candidate)) {
                set_cal_stage(BOARD_CAL_LEFT, now);
                vehicle_log_text("CAL ERR | invalid endpoints; capture left then right again");
                calibration_error(now);
                return;
            }
            s_draft = candidate;
            (void)vehicle_log_publish(&(const vehicle_log_message_t){
                .kind = VEHICLE_LOG_CAL_RIGHT, .data.raw = s_draft.right
            });
            save_calibration(now);
        }
    }
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
    /* Only immutable names and copied values cross the task boundary.
     * Raw-to-position conversion and every printf happen in the logger. */
    const vehicle_log_message_t message = {
        .kind = VEHICLE_LOG_STATUS,
        .data.status = {
            .remote_state = remote_state,
            .drive_state = drive_controller_state_name(s_drive.state),
            .steering_state = steering_state_name(),
            .calibration = s_saved,
            .measured_mm_s = right != NULL ? right->speed_mm_s : 0,
            .target_mm_s = s_drive.target_mm_s,
            .drive_pwm = s_drive.pwm,
            .steering_pwm = s_controller.pwm,
            .raw = encoder != NULL ? encoder->raw : 0U,
            .drive_mode = mode,
            .cal_stage = s_cal_stage,
            .position_valid = s_has_saved && encoder_ready(encoder),
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

static int16_t normal_steering(const remote_command_t *control,
                              const steering_encoder_sample_t *encoder, uint32_t now)
{
    const bool valid = s_has_saved && encoder_ready(encoder);
    const bool stick_neutral = (control->steering >= -VEHICLE_STICK_DEADBAND) &&
                               (control->steering <= VEHICLE_STICK_DEADBAND);
    if (!stick_neutral) {
        s_center_stable = false;
        const int16_t pwm = steering_controller_update(&s_controller, control->steering,
                                                       valid ? encoder->raw : 0U, valid, now);
        if (!s_controller.initialized) {
            /* A prolonged feedback fault must also disarm the old REMOTE
             * ownership. Recovery in neutral cannot start a new return. */
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
        /* Match the P controller's hold band, checking actual center rather
         * than a potentially old diagnostic error before its next update. */
        const int32_t offset = valid ?
            steering_calibration_delta(s_saved.center, encoder->raw) : 0;
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
    /* Startup, calibration exit and a completed return remain manual. Moving
     * the wheel by hand does not start another centering attempt. */
    steering_controller_stop(&s_controller);
    s_steering_mode = STEERING_MODE_MANUAL;
    return 0;
}

void vehicle_control_update(const remote_command_t *command,
                             const steering_encoder_sample_t *encoder,
                             const wheel_speed_sample_t *right_speed)
{
    const remote_command_t *control =
        (remote_command_valid(command) && (command->status == REMOTE_STATUS_OK)) ? command : NULL;
    const uint32_t now = HAL_GetTick();
    update_stability(control, encoder, now);
    process_button(control, encoder, now);

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
    } else if (s_cal_stage != BOARD_CAL_OFF) {
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
