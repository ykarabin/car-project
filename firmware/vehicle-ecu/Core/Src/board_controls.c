#include "board_controls.h"

#include <stdbool.h>

#include "main.h"

#define BUTTON_DEBOUNCE_MS UINT32_C(30)
#define BUTTON_LONG_MS UINT32_C(3000)
#define LED_GROUP_MS UINT32_C(1600)
#define LED_PULSE_MS UINT32_C(300)
#define LED_ON_MS UINT32_C(150)

static bool s_raw;
static bool s_pressed;
static bool s_armed;
static bool s_long_sent;
static uint32_t s_changed_at;
static uint32_t s_pressed_at;
static uint32_t s_pattern_at;
static uint32_t s_notice_at;
static board_cal_stage_t s_stage;
static board_notice_t s_notice;

void board_controls_init(uint32_t now_ms)
{
    s_raw = HAL_GPIO_ReadPin(CAL_BUTTON_GPIO_Port, CAL_BUTTON_Pin) == GPIO_PIN_SET;
    s_pressed = s_raw;
    s_armed = false;
    s_long_sent = false;
    s_changed_at = now_ms;
    s_pressed_at = now_ms;
    s_pattern_at = now_ms;
    s_notice_at = now_ms;
    s_stage = BOARD_CAL_OFF;
    s_notice = BOARD_NOTICE_NONE;
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET);
}

void board_controls_set_stage(board_cal_stage_t stage, uint32_t now_ms)
{
    s_stage = stage;
    s_pattern_at = now_ms;
    s_notice = BOARD_NOTICE_NONE;
}

void board_controls_signal(board_notice_t notice, uint32_t now_ms)
{
    s_notice = notice;
    s_notice_at = now_ms;
}

/* Notices temporarily override the calibration-stage blink pattern. */
static void update_led(uint32_t now_ms)
{
    const uint32_t notice_age = now_ms - s_notice_at;
    bool on;
    if ((s_notice == BOARD_NOTICE_ERROR) && (notice_age < 1000U)) {
        on = (notice_age % 100U) < 50U;
    } else if ((s_notice == BOARD_NOTICE_SAVED) && (notice_age < 2000U)) {
        on = true;
    } else {
        if (s_notice != BOARD_NOTICE_NONE) {
            s_notice = BOARD_NOTICE_NONE;
            s_pattern_at = now_ms;
        }
        const uint32_t phase = (now_ms - s_pattern_at) % LED_GROUP_MS;
        const uint32_t pulses = (uint32_t)s_stage;
        on = (phase < pulses * LED_PULSE_MS) && ((phase % LED_PULSE_MS) < LED_ON_MS);
    }
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

board_button_event_t board_controls_poll(uint32_t now_ms)
{
    board_button_event_t event = BOARD_BUTTON_NONE;
    const bool raw = HAL_GPIO_ReadPin(CAL_BUTTON_GPIO_Port, CAL_BUTTON_Pin) == GPIO_PIN_SET;
    if (raw != s_raw) {
        s_raw = raw;
        s_changed_at = now_ms;
    }
    if ((now_ms - s_changed_at) >= BUTTON_DEBOUNCE_MS) {
        if (s_pressed != s_raw) {
            s_pressed = s_raw;
            if (s_pressed) {
                s_pressed_at = now_ms;
                s_long_sent = false;
            } else {
                if (s_armed && !s_long_sent) {
                    event = BOARD_BUTTON_SHORT;
                }
                s_armed = true;
            }
        }
        if (!s_pressed) {
            s_armed = true;
        }
    }
    if (s_armed && s_pressed && s_raw && !s_long_sent &&
        ((now_ms - s_pressed_at) >= BUTTON_LONG_MS)) {
        s_long_sent = true;
        event = BOARD_BUTTON_LONG;
    }
    update_led(now_ms);
    return event;
}
