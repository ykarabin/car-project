#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "debug_console.h"
#include "vehicle_log.h"
#include "motor_control.h"
#include "steering_calibration_storage.h"
#include "vehicle_control.h"
#include "steering_controller_config.h"
#include "drive_controller_config.h"
#include "main.h"
#include "control_watchdog.h"

typedef enum { OUTPUT_DISABLED, OUTPUT_NORMAL, OUTPUT_SERVICE } output_mode_t;
static output_mode_t s_output;
static motor_request_t s_last_output;
static int16_t s_jog;
static bool s_steering_enabled;
static uint32_t s_now;
static char s_log[32768];
static size_t s_log_used;
static bool s_flash_valid;
static bool s_flash_fail;
static unsigned s_save_calls;
static steering_calibration_t s_flash;
static remote_command_t s_remote;
static steering_encoder_sample_t s_encoder;
static wheel_speed_sample_t s_right_speed;
static bool s_watchdog_reset;
GPIO_TypeDef test_gpioc;
GPIO_TypeDef test_gpiof;

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *gpio, uint16_t pin)
{
    assert(gpio == GPIOC && pin == GPIO_PIN_13);
    return (gpio->IDR & pin) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *gpio, uint16_t pin, GPIO_PinState state)
{
    assert(gpio == GPIOF && pin == GPIO_PIN_2);
    gpio->ODR = state == GPIO_PIN_SET ? pin : 0U;
}

uint32_t HAL_GetTick(void)
{
    return s_now;
}
bool control_watchdog_was_reset(void)
{
    return s_watchdog_reset;
}

/* Immediate consumer fake keeps the existing golden text assertions.
 * Queue ownership/copy/full/delayed timestamp are tested with real transport
 * separately in vehicle_log_test. Production control never formats. */
bool vehicle_log_publish(const vehicle_log_message_t *message)
{
    vehicle_log_format(HAL_GetTick(), message);
    return true;
}
void vehicle_log_text(const char *text)
{
    const vehicle_log_message_t message = {.kind = VEHICLE_LOG_TEXT, .data.text = text};
    (void)vehicle_log_publish(&message);
}

void debug_console_printf_at(uint32_t captured_at_ms, const char *format, ...)
{
    (void)captured_at_ms;
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(&s_log[s_log_used], sizeof(s_log) - s_log_used, format, args);
    va_end(args);
    assert(count >= 0 && (size_t)count < (sizeof(s_log) - s_log_used));
    s_log_used += (size_t)count;
}

void motor_control_apply(const motor_request_t *request)
{
    s_output = (request == NULL) ? OUTPUT_DISABLED :
                                   (request->rear_enabled ? OUTPUT_NORMAL : OUTPUT_SERVICE);
    s_steering_enabled = (request != NULL) && request->steering_enabled;
    if (request != NULL) {
        assert(request->rear_pwm >= -1000 && request->rear_pwm <= 1000);
        assert(request->steering_pwm >= -1000 && request->steering_pwm <= 1000);
        s_last_output = *request;
        if (!request->rear_enabled) {
            assert(request->rear_pwm == 0 && request->steering_enabled);
            s_jog = request->steering_pwm;
            assert(s_jog >= -150 && s_jog <= 150);
        }
    }
}

bool steering_calibration_storage_available(void)
{
    return true;
}
bool steering_calibration_storage_load(steering_calibration_t *calibration)
{
    if (!s_flash_valid) {
        return false;
    }
    *calibration = s_flash;
    return true;
}

bool steering_calibration_storage_save(const steering_calibration_t *calibration)
{
    assert(s_output == OUTPUT_DISABLED); /* Hardware must be off before Flash operations. */
    assert(steering_calibration_valid(calibration));
    ++s_save_calls;
    if (s_flash_fail) {
        return false;
    }
    s_flash = *calibration;
    s_flash_valid = true;
    return true;
}

static void tick(uint32_t elapsed)
{
    s_now += elapsed;
    vehicle_control_update(&s_remote, &s_encoder, &s_right_speed);
}

static void button(uint32_t hold_ms)
{
    test_gpioc.IDR = GPIO_PIN_13;
    tick(1U);
    tick(30U);
    while (hold_ms != 0U) {
        const uint32_t step = hold_ms < 10U ? hold_ms : 10U;
        tick(step);
        hold_ms -= step;
    }
    test_gpioc.IDR = 0U;
    tick(1U);
    tick(30U);
}

static void button_capture(uint16_t raw)
{
    s_encoder.raw = raw;
    s_remote.steering = 0;
    s_remote.throttle = 0;
    tick(0U);
    tick(150U);
    button(50U);
}

static void init(void)
{
    s_now = 0U;
    s_watchdog_reset = false;
    s_steering_enabled = false;
    s_log_used = 0U;
    s_log[0] = '\0';
    s_flash_valid = false;
    s_flash_fail = false;
    s_save_calls = 0U;
    s_remote = (remote_command_t){.drive_mode = 1U, .status = REMOTE_STATUS_OK};
    s_encoder = (steering_encoder_sample_t){.state = STEERING_ENCODER_OK, .raw = 1000U};
    s_right_speed = (wheel_speed_sample_t){.state = WHEEL_SPEED_OK, .interval_ms = 10U};
    test_gpioc.IDR = 0U;
    test_gpiof.ODR = 0U;
    vehicle_control_init();
    tick(0U);
    tick(30U); /* Arm the initially released button. */
}

static void test_calibration_neutral_boundary(void)
{
    const int16_t requests[] = {-31, -30, -29, 0, 29, 30, 31};
    for (uint32_t axis = 0U; axis < 2U; ++axis) {
        for (size_t i = 0U; i < sizeof(requests) / sizeof(requests[0]); ++i) {
            init();
            if (axis == 0U) {
                s_remote.throttle = requests[i];
            } else {
                s_remote.steering = requests[i];
            }
            button(3000U);
            if (requests[i] >= -30 && requests[i] <= 30) {
                assert(s_output == OUTPUT_SERVICE);
                assert(strstr(s_log, "CAL STARTED") != NULL);
            } else {
                assert(s_output != OUTPUT_SERVICE);
                assert(strstr(s_log, "calibration needs") != NULL);
            }
            assert(s_save_calls == 0U);
        }
    }
}

static void calibrate(void)
{
    button(3000U);
    button_capture(1000U);
    button_capture(500U);
    button_capture(1600U);
    assert(s_flash_valid && s_save_calls == 1U);
}

static void test_capture_save_reload_and_limits(void)
{
    init();
    s_remote.steering = 800;
    s_remote.throttle = 500;
    tick(0U);
    assert(s_last_output.rear_pwm == 0 &&
           s_last_output.steering_pwm == 0); /* Drive first seeds time. */
    tick(10U);
    assert(s_last_output.rear_pwm > 0 && s_last_output.rear_pwm <= DRIVE_CONTROL_PWM_RISE_STEP);
    button(3000U);
    assert(strstr(s_log, "calibration needs") != NULL);
    s_remote.steering = 0;
    s_remote.throttle = 0;
    button(3000U);
    s_remote.steering = -1000;
    s_remote.throttle = 1000;
    tick(0U);
    assert(s_output == OUTPUT_SERVICE && s_jog == -150);
    button(50U);
    assert(strstr(s_log, "need fresh, steady") != NULL);
    button_capture(1000U);
    button_capture(500U);
    button_capture(1600U);
    assert(s_output == OUTPUT_DISABLED && s_flash_valid && s_save_calls == 1U);
    assert(s_flash.center == 1000U && s_flash.left == 500U && s_flash.right == 1600U);
    assert(strstr(s_log, "CAL SAVED") != NULL);
    s_remote.steering = 1000;
    tick(1U);
    tick(10U);
    tick(10U);
    assert(s_last_output.steering_pwm <
           0); /* Pull physical right endpoint inside the working range. */
    s_remote.steering = -1000;
    tick(10U);
    assert(s_last_output.steering_pwm < 0 &&
           s_last_output.steering_pwm >= -STEERING_CONTROL_PWM_MAX);
    s_encoder.raw = 500U;
    tick(10U);
    assert(s_last_output.steering_pwm >=
           0); /* Outward negative drive at the left limit is blocked. */
    s_remote.steering = 1000;
    tick(10U);
    assert(s_last_output.steering_pwm > 0 &&
           s_last_output.steering_pwm <= STEERING_CONTROL_PWM_MAX);
    s_encoder.state = STEERING_ENCODER_STALE;
    tick(1U);
    assert(s_last_output.steering_pwm > 0);
    tick(40U);
    assert(s_last_output.steering_pwm == 0);
    s_encoder.state = STEERING_ENCODER_OK;
    vehicle_control_init(); /* Startup reloads previously saved points. */
    const size_t before_status = s_log_used;
    vehicle_control_log_status("OK", s_remote.drive_mode, &s_encoder, &s_right_speed);
    assert(strstr(s_log + before_status, "pos=-1000/1000") != NULL);
}

static void test_partial_geometry_and_failed_save_keep_previous_record(void)
{
    init();
    calibrate();
    const steering_calibration_t saved = s_flash;
    button(3000U);
    button_capture(1050U);
    button_capture(1060U); /* Too close to center: remain at the left step. */
    assert(s_save_calls == 1U && strstr(s_log, "left endpoint too close") != NULL);
    button_capture(1150U);
    button_capture(1650U); /* Both ends are on the same side: repeat both. */
    assert(s_save_calls == 1U && strstr(s_log, "invalid endpoints; capture left") != NULL);
    button_capture(550U);
    s_flash_fail = true;
    button_capture(1650U);
    assert(s_save_calls == 2U && s_output == OUTPUT_SERVICE && s_jog == 0);
    assert(s_flash.center == saved.center && s_flash.left == saved.left &&
           s_flash.right == saved.right);
    s_flash_fail = false;
    button(50U); /* Retry the right point and automatic save. */
    assert(s_save_calls == 3U && s_flash_valid);
    assert(s_flash.center == 1050U && s_flash.left == 550U && s_flash.right == 1650U);
}

static void test_cancel_remote_loss_and_encoder_loss(void)
{
    init();
    calibrate();
    const steering_calibration_t saved = s_flash;
    button(3000U);
    s_remote.steering = 1000;
    tick(1U);
    assert(s_output == OUTPUT_SERVICE && s_jog == 150);
    s_encoder.state = STEERING_ENCODER_INVALID;
    tick(1U);
    assert(s_output == OUTPUT_DISABLED);
    s_encoder.state = STEERING_ENCODER_OK;
    s_remote.status = REMOTE_STATUS_ERROR;
    tick(1U);
    assert(s_output == OUTPUT_DISABLED);
    s_remote.status = REMOTE_STATUS_OK;
    s_remote.throttle = 1000;
    button(3000U);
    tick(1U);
    assert(s_output == OUTPUT_DISABLED); /* Exit cannot suddenly re-enable held throttle. */
    s_remote.steering = 0;
    s_remote.throttle = 0;
    tick(1U);
    assert(s_output == OUTPUT_DISABLED);
    tick(1U);
    assert(s_output == OUTPUT_NORMAL);
    assert(s_flash.center == saved.center && s_flash.left == saved.left &&
           s_flash.right == saved.right);
    assert(s_save_calls == 1U);
}

static void test_button_calibration_clock_wrap_and_cancel_without_feedback(void)
{
    init();
    s_now = UINT32_MAX - 50U;
    button(3000U);
    button_capture(1000U);
    assert(strstr(s_log, "CAL CENTER | raw=1000") != NULL);
    s_encoder.state = STEERING_ENCODER_STALE;
    s_remote.steering = 1000;
    tick(1U);
    assert(s_output == OUTPUT_DISABLED);
    button(3000U); /* Cancellation must remain possible without valid feedback. */
    assert(strstr(s_log, "CAL CANCELLED") != NULL && s_save_calls == 0U);
    s_remote.steering = 0;
    s_encoder.state = STEERING_ENCODER_OK;
    tick(1U);
    tick(1U);
    assert(s_output == OUTPUT_NORMAL && !s_steering_enabled);
}

static void test_feedback_loss_keeps_steering_and_service_resets_drive(void)
{
    init();
    calibrate();
    tick(1U); /* Clear post-save neutral wait. */
    s_encoder.raw = 1000U;
    s_remote.steering = 500;
    s_remote.throttle = 1000;
    tick(10U);
    tick(10U);
    assert(s_output == OUTPUT_NORMAL && s_last_output.rear_pwm > 0);
    s_right_speed.state = WHEEL_SPEED_STALE;
    tick(1U);
    assert(s_output == OUTPUT_NORMAL && s_last_output.rear_pwm > 0);
    tick(40U);
    assert(s_output == OUTPUT_NORMAL && s_last_output.rear_pwm == 0);
    assert(s_last_output.steering_pwm > 0); /* Rear feedback loss does not disable steering. */
    s_right_speed.state = WHEEL_SPEED_OK;
    s_remote.steering = 0;
    s_remote.throttle = 0;
    button(3000U);
    assert(s_output == OUTPUT_SERVICE);
    s_remote.steering = 1000;
    s_remote.throttle = 1000;
    tick(10U);
    assert(s_output == OUTPUT_SERVICE && s_jog == 150);
    button(3000U);
    assert(s_output == OUTPUT_DISABLED);
    s_remote.steering = 0;
    s_remote.throttle = 0;
    tick(1U);
    tick(1U);
    s_remote.throttle = 1000;
    tick(10U);
    assert(s_last_output.rear_pwm == 0);
    tick(10U);
    assert(s_last_output.rear_pwm > 0 && s_last_output.rear_pwm <= DRIVE_CONTROL_PWM_RISE_STEP);
    s_remote.status = REMOTE_STATUS_ERROR;
    tick(1U);
    assert(s_output == OUTPUT_DISABLED);
}

static void test_reverse_brake_gesture_reaches_motor_output(void)
{
    init();
    calibrate();
    tick(1U);
    s_remote.drive_mode = 3U;
    s_remote.throttle = 1000;
    tick(10U);
    for (uint32_t i = 0U; i < 30U; ++i) {
        tick(10U);
    }
    const int16_t before = s_last_output.rear_pwm;
    assert(before > 100);
    s_remote.throttle = -1000;
    tick(10U);
    assert(s_output == OUTPUT_NORMAL);
    assert(s_last_output.rear_pwm == before - DRIVE_CONTROL_PWM_BRAKE_STEP);
    for (uint32_t i = 0U; i < 100U; ++i) {
        tick(10U);
        assert(s_output == OUTPUT_NORMAL && s_last_output.rear_pwm >= 0);
    }
    assert(s_last_output.rear_pwm == 0);
    s_remote.throttle = 0;
    tick(10U);
    s_remote.throttle = -1000;
    tick(10U);
    for (uint32_t i = 0U; i < 10U; ++i) {
        tick(10U);
        assert(s_last_output.rear_pwm == 0);
    }
    tick(10U);
    assert(s_last_output.rear_pwm < 0);
    s_remote.status = REMOTE_STATUS_ERROR;
    tick(1U);
    assert(s_output == OUTPUT_DISABLED);
    s_remote.status = REMOTE_STATUS_OK;
    for (uint32_t i = 0U; i < 30U; ++i) {
        tick(10U);
        assert(s_output == OUTPUT_DISABLED);
    }
    s_remote.throttle = 0;
    tick(1U);
    assert(s_output == OUTPUT_DISABLED);
    tick(1U);
    s_remote.throttle = -1000;
    for (uint32_t i = 0U; i < 30U; ++i) {
        tick(10U);
        assert(s_output == OUTPUT_NORMAL && s_last_output.rear_pwm == 0);
    }
}

static void test_neutral_stop_then_first_reverse_reaches_motor_output(void)
{
    init();
    calibrate();
    tick(1U);
    s_remote.drive_mode = 3U;
    s_remote.throttle = 1000;
    tick(10U);
    for (uint32_t i = 0U; i < 30U; ++i) {
        tick(10U);
    }
    assert(s_last_output.rear_pwm > 100);
    s_remote.throttle = 0;
    for (uint32_t i = 0U; i < 200U; ++i) {
        tick(10U);
    }
    assert(s_output == OUTPUT_NORMAL && s_last_output.rear_pwm == 0);
    vehicle_control_log_status("OK", s_remote.drive_mode, &s_encoder, &s_right_speed);
    assert(strstr(s_log, "DRIVE STOP") != NULL);
    s_remote.throttle = -1000;
    for (uint32_t i = 0U; i < 13U; ++i) {
        tick(10U);
    }
    assert(s_output == OUTPUT_NORMAL && s_last_output.rear_pwm < 0);
}

static void finish_center_return(void)
{
    s_remote.steering = 0;
    s_encoder.raw = s_flash.center;
    tick(1U);
    for (uint32_t elapsed = 0U;
         elapsed <= STEERING_CONTROL_RELEASE_SETTLE_MS + 2U * STEERING_CONTROL_PERIOD_MS;
         elapsed += STEERING_CONTROL_PERIOD_MS) {
        tick(STEERING_CONTROL_PERIOD_MS);
    }
    assert(!s_steering_enabled && s_last_output.steering_pwm == 0);
}

static void test_manual_steering_preserves_remote_throttle_and_resets_takeover(void)
{
    init();
    calibrate();
    tick(1U);
    s_remote.throttle = 800;
    s_encoder.raw = 500U;
    tick(10U);
    tick(10U);
    assert(s_output == OUTPUT_NORMAL && s_last_output.rear_pwm > 0);
    assert(!s_steering_enabled && s_last_output.steering_pwm == 0);
    const uint16_t manual_positions[] = {500U, 1600U, 0U, 4095U, 1200U};
    for (size_t i = 0U; i < sizeof(manual_positions) / sizeof(manual_positions[0]); ++i) {
        const int16_t before = s_last_output.rear_pwm;
        s_encoder.raw = manual_positions[i];
        tick(10U);
        assert(!s_steering_enabled && s_last_output.steering_pwm == 0);
        assert(s_last_output.rear_pwm >= before &&
               s_last_output.rear_pwm <= before + DRIVE_CONTROL_PWM_RISE_STEP);
    }
    vehicle_control_log_status("OK", s_remote.drive_mode, &s_encoder, &s_right_speed);
    assert(strstr(s_log, "STEER MANUAL") != NULL);
    s_remote.steering = 1000;
    tick(1U); /* Takeover seeds from the actual 1200; no stale controller output. */
    assert(s_steering_enabled && s_last_output.steering_pwm == 0);
    tick(10U);
    assert(s_steering_enabled && s_last_output.steering_pwm == STEERING_CONTROL_PWM_STEP);
    s_encoder.raw = 1582U; /* Working right target for the test calibration. */
    tick(10U);
    assert(s_steering_enabled &&
           s_last_output.steering_pwm == 0); /* Hold is still remote ownership. */
    s_remote.steering = 0;
    tick(1U);
    assert(s_steering_enabled); /* Centering precedes manual release. */
    finish_center_return();
    s_encoder.raw = 600U;
    tick(10U);
    assert(!s_steering_enabled && s_last_output.steering_pwm == 0);
    s_remote.steering = -1000;
    tick(1U);
    assert(s_steering_enabled && s_last_output.steering_pwm == 0);
    tick(10U);
    assert(s_last_output.steering_pwm == -STEERING_CONTROL_PWM_STEP);
}

static void test_manual_steering_deadband_feedback_and_remote_loss(void)
{
    init();
    calibrate();
    tick(1U);
    s_encoder.raw = 1000U;
    const int16_t neutral_inputs[] = {-30, -1, 0, 1, 30};
    for (size_t i = 0U; i < sizeof(neutral_inputs) / sizeof(neutral_inputs[0]); ++i) {
        s_remote.steering = neutral_inputs[i];
        tick(10U);
        assert(!s_steering_enabled && s_last_output.steering_pwm == 0);
    }
    s_remote.steering = 31;
    tick(10U);
    assert(s_steering_enabled);
    s_remote.steering = -30;
    tick(1U);
    assert(s_steering_enabled);
    finish_center_return();
    s_remote.steering = -31;
    tick(10U);
    assert(s_steering_enabled);
    s_remote.steering = 1000;
    tick(10U);
    tick(10U);
    assert(s_last_output.steering_pwm > 0);
    s_encoder.state = STEERING_ENCODER_INVALID;
    s_remote.steering = 0;
    tick(1U); /* A brief feedback loss may finish recovering before release. */
    assert(s_steering_enabled);
    for (uint32_t i = 0U; i < 4U; ++i) {
        tick(10U);
    }
    assert(!s_steering_enabled && s_last_output.steering_pwm == 0);
    assert(strstr(s_log, "STEER_RELEASE FEEDBACK") != NULL);
    s_remote.throttle = 1000;
    for (uint32_t i = 0U; i < 10U; ++i) {
        tick(10U);
    }
    assert(s_output == OUTPUT_NORMAL && s_last_output.rear_pwm > 0 && !s_steering_enabled);
    s_remote.status = REMOTE_STATUS_ERROR;
    tick(1U);
    assert(s_output == OUTPUT_DISABLED && !s_steering_enabled);
    s_remote.status = REMOTE_STATUS_OK;
    s_remote.throttle = 0;
    tick(1U);
    assert(s_output == OUTPUT_DISABLED && !s_steering_enabled);
    tick(1U);
    assert(s_output == OUTPUT_NORMAL && !s_steering_enabled);
}

static void start_remote_right_hold(void)
{
    init();
    calibrate();
    tick(1U);
    s_encoder.raw = 1582U;
    s_remote.steering = 1000;
    tick(10U);
    tick(10U);
    assert(s_steering_enabled && s_last_output.steering_pwm == 0);
}

static void test_tracking_feedback_timeout_does_not_arm_center_return(void)
{
    for (uint32_t wrap = 0U; wrap < 2U; ++wrap) {
        start_remote_right_hold();
        if (wrap != 0U) {
            s_now = UINT32_MAX - 70U;
            tick(1U); /* Re-seed the controller after the artificial clock jump. */
            tick(10U);
        }
        s_remote.throttle = 800;
        tick(10U);
        tick(10U);
        assert(s_last_output.rear_pwm > 0);
        s_encoder.state = STEERING_ENCODER_INVALID;
        for (uint32_t i = 0U; i < 3U; ++i) {
            tick(10U);
        }
        tick(9U);
        assert(s_steering_enabled); /* 39 ms retains the existing short-fault grace. */
        tick(1U);
        assert(!s_steering_enabled && s_last_output.steering_pwm == 0);
        assert(s_last_output.rear_pwm > 0);
        assert(strstr(s_log, "STEER_RELEASE FEEDBACK") != NULL);
        const size_t log_after_fault = s_log_used;
        for (uint32_t i = 0U; i < 10U; ++i) {
            tick(10U);
        }
        assert(!s_steering_enabled && s_log_used == log_after_fault);

        /* Recovery with a neutral stick must not turn the old REMOTE state
         * into a new centering request. No new stick deflection occurred. */
        s_remote.steering = 0;
        s_encoder.state = STEERING_ENCODER_OK;
        for (uint32_t i = 0U; i < 20U; ++i) {
            tick(10U);
            assert(!s_steering_enabled && s_last_output.steering_pwm == 0);
            assert(s_last_output.rear_pwm > 0);
        }
        s_encoder.raw = 600U;
        tick(10U); /* Manual movement also cannot restart centering. */
        assert(!s_steering_enabled && s_last_output.steering_pwm == 0);

        s_remote.steering = 1000;
        tick(1U); /* A fresh nonneutral command takes over from the actual angle. */
        assert(s_steering_enabled && s_last_output.steering_pwm == 0);
        tick(10U);
        assert(s_last_output.steering_pwm > 0);
        s_remote.steering = 0;
        tick(10U);
        tick(10U);
        assert(s_steering_enabled && s_last_output.steering_pwm > 0);
    }
}

static void test_short_tracking_feedback_loss_still_centers_on_release(void)
{
    start_remote_right_hold();
    s_encoder.state = STEERING_ENCODER_INVALID;
    tick(10U);
    tick(10U);
    tick(10U);
    tick(9U);
    assert(s_steering_enabled);
    s_remote.steering = 0;
    s_encoder.state = STEERING_ENCODER_OK;
    tick(1U);
    tick(10U);
    assert(s_steering_enabled && s_last_output.steering_pwm < 0);
    assert(strstr(s_log, "STEER_RELEASE FEEDBACK") == NULL);
}

static void test_center_return_settles_then_stays_manual_with_remote_throttle(void)
{
    start_remote_right_hold();
    s_remote.throttle = 800;
    s_remote.steering = 0;
    tick(1U);
    assert(s_steering_enabled); /* Off-center neutral starts a bounded return. */
    tick(10U);
    assert(s_last_output.steering_pwm < 0 && s_last_output.rear_pwm > 0);
    vehicle_control_log_status("OK", s_remote.drive_mode, &s_encoder, &s_right_speed);
    assert(strstr(s_log, "STEER CENTER") != NULL);
    s_encoder.raw = 1000U;
    tick(10U);
    for (uint32_t i = 0U; i < 9U; ++i) {
        tick(10U);
        assert(s_steering_enabled && s_last_output.steering_pwm == 0);
    }
    s_encoder.raw = 1010U; /* Leaving center restarts the settle interval. */
    tick(10U);
    assert(s_steering_enabled && s_last_output.steering_pwm < 0);
    s_encoder.raw = 1000U;
    tick(10U);
    s_encoder.state = STEERING_ENCODER_INVALID;
    tick(1U);
    assert(s_steering_enabled);
    s_encoder.state = STEERING_ENCODER_OK;
    tick(1U);
    for (uint32_t i = 0U; i < 9U; ++i) {
        tick(10U);
        assert(s_steering_enabled);
    }
    tick(9U);
    assert(s_steering_enabled);
    tick(1U);
    assert(!s_steering_enabled && s_last_output.steering_pwm == 0);
    assert(strstr(s_log, "STEER_RELEASE CENTERED") != NULL);
    s_encoder.raw = 500U; /* Subsequent hand movement must not re-center. */
    for (uint32_t i = 0U; i < 50U; ++i) {
        tick(10U);
        assert(!s_steering_enabled && s_last_output.steering_pwm == 0);
        assert(s_last_output.rear_pwm > 0);
    }
}

static void test_center_return_timeout_and_clock_wrap(void)
{
    for (uint32_t wrap = 0U; wrap < 2U; ++wrap) {
        start_remote_right_hold();
        if (wrap != 0U) {
            s_now = UINT32_MAX - 50U;
            tick(1U);
        }
        s_remote.steering = 0;
        s_remote.throttle = 500;
        tick(1U);
        for (uint32_t elapsed = 10U; elapsed < STEERING_CONTROL_RELEASE_TIMEOUT_MS;
             elapsed += 10U) {
            tick(10U);
            assert(s_steering_enabled && s_last_output.steering_pwm <= 0);
        }
        tick(9U);
        assert(s_steering_enabled);
        tick(1U);
        assert(!s_steering_enabled && s_last_output.steering_pwm == 0);
        assert(s_last_output.rear_pwm > 0);
        assert(strstr(s_log, "STEER_RELEASE TIMEOUT") != NULL);
        for (uint32_t i = 0U; i < 50U; ++i) {
            tick(10U);
            assert(!s_steering_enabled && s_last_output.steering_pwm == 0);
        }
    }
}

static void test_remote_input_cancels_center_return_and_next_release_gets_new_deadline(void)
{
    start_remote_right_hold();
    s_remote.steering = 0;
    tick(1U);
    tick(10U);
    s_remote.steering = 500;
    for (uint32_t elapsed = 0U; elapsed <= STEERING_CONTROL_RELEASE_TIMEOUT_MS + 100U;
         elapsed += 10U) {
        tick(10U);
        assert(s_steering_enabled);
    }
    assert(strstr(s_log, "STEER_RELEASE TIMEOUT") == NULL);
    finish_center_return();
    assert(strstr(s_log, "STEER_RELEASE CENTERED") != NULL);
    assert(strstr(s_log, "STEER_RELEASE TIMEOUT") == NULL);
}

static void test_fault_and_service_cancel_center_return_without_restart(void)
{
    for (uint32_t service = 0U; service < 2U; ++service) {
        start_remote_right_hold();
        if (service != 0U) {
            test_gpioc.IDR = GPIO_PIN_13;
            tick(1U);
            tick(30U);
            for (uint32_t i = 0U; i < 280U; ++i) {
                tick(10U);
            }
        }
        s_remote.steering = 0;
        tick(1U);
        tick(10U);
        assert(s_last_output.steering_pwm < 0);
        if (service == 0U) {
            s_remote.status = REMOTE_STATUS_ERROR;
            tick(1U);
            assert(s_output == OUTPUT_DISABLED);
            s_remote.status = REMOTE_STATUS_OK;
            tick(1U);
            assert(s_output == OUTPUT_DISABLED);
        } else {
            for (uint32_t i = 0U; i < 19U; ++i) {
                tick(10U);
            }
            assert(s_output == OUTPUT_SERVICE);
            assert(strstr(s_log, "STEER_RELEASE TIMEOUT") == NULL);
            test_gpioc.IDR = 0U;
            tick(1U);
            tick(30U);
            button(3000U);
        }
        for (uint32_t i = 0U; i < 20U; ++i) {
            tick(10U);
            assert(s_output == OUTPUT_NORMAL && !s_steering_enabled);
            assert(s_last_output.steering_pwm == 0);
        }
    }
}

static void test_button_three_points_save_reload_and_cancel(void)
{
    init();
    tick(30U);
    button(3000U);
    assert(strstr(s_log, "CAL STARTED") != NULL && s_output == OUTPUT_SERVICE);
    assert(strstr(s_log, "CAL CENTER |") == NULL); /* Releasing start cannot capture. */
    s_remote.steering = -1000;
    s_remote.throttle = 1000;
    tick(10U);
    assert(s_output == OUTPUT_SERVICE && s_jog == -150);
    button_capture(56U);
    button_capture(3654U);
    assert(s_save_calls == 0U);
    button_capture(548U);
    assert(s_save_calls == 1U && s_flash_valid && s_output == OUTPUT_DISABLED);
    assert(s_flash.center == 56U && s_flash.left == 3654U && s_flash.right == 548U);
    tick(1U);
    assert(s_output == OUTPUT_NORMAL);
    vehicle_control_init();
    s_encoder.raw = 56U;
    const size_t before_status = s_log_used;
    vehicle_control_log_status("OK", s_remote.drive_mode, &s_encoder, &s_right_speed);
    assert(strstr(s_log + before_status, "pos=0/1000") != NULL);
    tick(30U);
    button(3000U);
    button_capture(60U);
    s_remote.throttle = 1000;
    button(3000U);
    assert(s_output == OUTPUT_DISABLED && s_save_calls == 1U);
    tick(10U);
    assert(s_output == OUTPUT_DISABLED); /* Held throttle cannot leave cancel. */
    s_remote.throttle = 0;
    tick(1U);
    tick(1U);
    assert(s_output == OUTPUT_NORMAL);
    assert(s_flash.center == 56U && s_flash.left == 3654U && s_flash.right == 548U);
}

static void test_button_rejects_unready_capture_and_failed_save(void)
{
    init();
    tick(30U);
    s_remote.throttle = 100;
    button(3000U);
    assert(strstr(s_log, "calibration needs") != NULL && s_save_calls == 0U);
    s_remote.throttle = 0;
    s_encoder.state = STEERING_ENCODER_INVALID;
    button(3000U);
    assert(s_save_calls == 0U && s_output == OUTPUT_NORMAL);
    s_encoder.state = STEERING_ENCODER_OK;
    button(3000U);
    s_remote.steering = 500;
    button(50U);
    assert(strstr(s_log, "need fresh, steady") != NULL);
    button_capture(1000U);
    button_capture(1010U);
    assert(strstr(s_log, "left endpoint too close") != NULL);
    button_capture(500U);
    button_capture(700U); /* Wrong side for right: return to the left step. */
    assert(strstr(s_log, "invalid endpoints; capture left") != NULL && s_save_calls == 0U);
    button_capture(500U);
    s_flash_fail = true;
    button_capture(1600U);
    assert(s_save_calls == 1U && !s_flash_valid && s_output == OUTPUT_SERVICE);
    s_flash_fail = false;
    button_capture(1600U);
    assert(s_save_calls == 2U && s_flash_valid);
}

static void test_button_held_at_boot_does_not_enter_or_write(void)
{
    init();
    test_gpioc.IDR = GPIO_PIN_13;
    vehicle_control_init();
    tick(4000U);
    assert(strstr(s_log, "CAL STARTED") == NULL && s_save_calls == 0U);
    test_gpioc.IDR = 0U;
    tick(1U);
    tick(30U);
    assert(strstr(s_log, "CAL STARTED") == NULL);
    button(3000U);
    assert(strstr(s_log, "CAL STARTED") != NULL && s_save_calls == 0U);
}

static void test_normal_startup_requires_neutral_before_outputs(void)
{
    init();
    calibrate();
    s_remote.throttle = 1000;
    s_remote.steering = 1000;
    vehicle_control_init();
    vehicle_control_update(NULL, &s_encoder, &s_right_speed);
    for (uint32_t i = 0U; i < 10U; ++i) {
        tick(10U);
        assert(s_output == OUTPUT_DISABLED && !s_steering_enabled);
    }
    s_remote.throttle = 0;
    tick(10U);
    assert(s_output == OUTPUT_DISABLED);
    s_remote.steering = -31;
    tick(10U);
    assert(s_output == OUTPUT_DISABLED);
    s_remote.steering = -30;
    s_remote.throttle = 30;
    tick(10U);
    assert(s_output == OUTPUT_DISABLED);
    tick(10U);
    assert(s_output == OUTPUT_NORMAL && s_last_output.rear_pwm == 0 && !s_steering_enabled);
    s_remote.throttle = 1000;
    tick(10U);
    tick(10U);
    assert(s_last_output.rear_pwm > 0);
}

static void test_control_loss_requires_new_neutral(void)
{
    for (unsigned loss = 0U; loss < 4U; ++loss) {
        init();
        calibrate();
        s_remote.throttle = 1000;
        s_remote.steering = 1000;
        tick(10U);
        tick(10U);
        assert(s_output == OUTPUT_NORMAL && s_last_output.rear_pwm > 0);

        remote_command_t failed = {.drive_mode = 1U, .status = REMOTE_STATUS_ERROR};
        if (loss == 2U) {
            failed = s_remote;
            failed.throttle = 1001;
        } else if (loss == 3U) {
            failed = s_remote;
            failed.drive_mode = 0U;
        }
        vehicle_control_update(loss == 0U ? NULL : &failed, &s_encoder, &s_right_speed);
        assert(s_output == OUTPUT_DISABLED && !s_steering_enabled);
        for (unsigned i = 0U; i < 10U; ++i) {
            tick(10U);
            assert(s_output == OUTPUT_DISABLED && !s_steering_enabled);
        }

        /* A neutral ERROR frame cannot acknowledge the neutral wait. */
        failed = (remote_command_t){.drive_mode = 1U, .status = REMOTE_STATUS_ERROR};
        vehicle_control_update(&failed, &s_encoder, &s_right_speed);
        tick(10U);
        assert(s_output == OUTPUT_DISABLED);
        s_remote.throttle = 0;
        tick(10U);
        assert(s_output == OUTPUT_DISABLED);
        s_remote.steering = 0;
        tick(10U);
        assert(s_output == OUTPUT_DISABLED);
        tick(10U);
        assert(s_output == OUTPUT_NORMAL && s_last_output.rear_pwm == 0 && !s_steering_enabled);

        /* A later outage requires another neutral acknowledgement. */
        vehicle_control_update(NULL, &s_encoder, &s_right_speed);
        s_remote.throttle = 1000;
        tick(10U);
        assert(s_output == OUTPUT_DISABLED);
        s_remote.throttle = 0;
        tick(10U);
        tick(10U);
        s_remote.throttle = 1000;
        tick(10U);
        tick(10U);
        assert(s_output == OUTPUT_NORMAL && s_last_output.rear_pwm > 0);
    }
}

static void test_watchdog_reboot_requires_neutral_before_outputs(void)
{
    init();
    calibrate();
    s_watchdog_reset = true;
    s_remote.throttle = 1000;
    s_remote.steering = 1000;
    vehicle_control_init();
    for (uint32_t i = 0U; i < 100U; ++i) {
        tick(10U);
        assert(s_output == OUTPUT_DISABLED);
    }
    s_remote.throttle = 0;
    tick(10U);
    assert(s_output == OUTPUT_DISABLED); /* Steering must also be neutral. */
    s_remote.steering = 0;
    tick(10U);
    assert(s_output == OUTPUT_DISABLED);
    tick(10U);
    assert(s_output == OUTPUT_NORMAL && !s_steering_enabled);
    s_remote.throttle = -1000;
    for (uint32_t i = 0U; i < 30U; ++i) {
        tick(10U);
        assert(s_last_output.rear_pwm == 0); /* Startup reverse gesture still required. */
    }
    s_remote.throttle = 0;
    tick(10U);
    s_remote.throttle = 1000;
    tick(10U);
    tick(10U);
    assert(s_last_output.rear_pwm > 0);
    assert(strstr(s_log, "WATCHDOG RESET | release both sticks") != NULL);
}

static void test_compact_status_values_and_unavailable_feedback(void)
{
    init();
    calibrate();
    s_encoder.raw = 1000U;
    tick(1U);
    s_right_speed.speed_mm_s = -1111;
    s_remote.throttle = 1000;
    tick(10U);
    size_t start = s_log_used;
    vehicle_control_log_status("OK", 1U, &s_encoder, &s_right_speed);
    assert(strstr(s_log + start, "v=-4.0kmh target=4.0kmh") != NULL);
    assert(strstr(s_log + start, "pos=0/1000") != NULL);
    s_right_speed.age_ms = DRIVE_CONTROL_FEEDBACK_MAX_AGE_MS;
    s_encoder.state = STEERING_ENCODER_STALE;
    start = s_log_used;
    vehicle_control_log_status("OK", 1U, &s_encoder, &s_right_speed);
    assert(strstr(s_log + start, "v=--kmh target=4.0kmh") != NULL);
    assert(strstr(s_log + start, "pos=--") != NULL);
    s_right_speed.age_ms = 0U;
    s_right_speed.speed_mm_s = 0;
    s_remote.drive_mode = 3U;
    tick(10U);
    start = s_log_used;
    vehicle_control_log_status("OK", 3U, &s_encoder, &s_right_speed);
    assert(strstr(s_log + start, "v=0.0kmh target=MAX") != NULL);
    s_remote.throttle = 0;
    s_remote.steering = 0;
    s_encoder.state = STEERING_ENCODER_OK;
    button(3000U);
    start = s_log_used;
    vehicle_control_log_status("OK", 3U, &s_encoder, &s_right_speed);
    assert(strstr(s_log + start, "CAL CENTER") != NULL);
    assert(strstr(s_log + start, "rear=OFF") != NULL);
}

int main(void)
{
    test_calibration_neutral_boundary();
    test_capture_save_reload_and_limits();
    test_partial_geometry_and_failed_save_keep_previous_record();
    test_cancel_remote_loss_and_encoder_loss();
    test_button_calibration_clock_wrap_and_cancel_without_feedback();
    test_feedback_loss_keeps_steering_and_service_resets_drive();
    test_reverse_brake_gesture_reaches_motor_output();
    test_neutral_stop_then_first_reverse_reaches_motor_output();
    test_manual_steering_preserves_remote_throttle_and_resets_takeover();
    test_manual_steering_deadband_feedback_and_remote_loss();
    test_tracking_feedback_timeout_does_not_arm_center_return();
    test_short_tracking_feedback_loss_still_centers_on_release();
    test_center_return_settles_then_stays_manual_with_remote_throttle();
    test_center_return_timeout_and_clock_wrap();
    test_remote_input_cancels_center_return_and_next_release_gets_new_deadline();
    test_fault_and_service_cancel_center_return_without_restart();
    test_button_three_points_save_reload_and_cancel();
    test_button_rejects_unready_capture_and_failed_save();
    test_button_held_at_boot_does_not_enter_or_write();
    test_normal_startup_requires_neutral_before_outputs();
    test_control_loss_requires_new_neutral();
    test_watchdog_reboot_requires_neutral_before_outputs();
    test_compact_status_values_and_unavailable_feedback();
    return 0;
}
