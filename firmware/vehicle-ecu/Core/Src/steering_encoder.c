#include "steering_encoder.h"

#include <stddef.h>

#include "mt6701_pwm.h"

#define CAPTURE_FLAGS (TIM_FLAG_CC1 | TIM_FLAG_CC2 | TIM_FLAG_CC1OF | TIM_FLAG_CC2OF)
#define OVERRUN_FLAGS (TIM_FLAG_CC1OF | TIM_FLAG_CC2OF)

typedef struct {
    uint32_t period_ticks;
    uint32_t high_ticks;
    uint32_t captured_at_ms;
    bool paired;
    bool has_frame;
} encoder_capture_t;

static TIM_HandleTypeDef *s_timer;
static volatile encoder_capture_t s_capture;
static bool s_synchronized;
static uint32_t s_last_edge_ms;

bool steering_encoder_init(TIM_HandleTypeDef *timer)
{
    if ((timer == NULL) || (timer->Instance != TIM4)) {
        return false;
    }
    s_capture = (encoder_capture_t){0};
    s_synchronized = false;
    s_last_edge_ms = HAL_GetTick();
    s_timer = timer;
    __HAL_TIM_CLEAR_FLAG(timer, CAPTURE_FLAGS);

    /* Keep CC2 pending until the next rising edge to detect mismatched captures. */
    if (HAL_TIM_IC_Start(timer, TIM_CHANNEL_2) != HAL_OK) {
        s_timer = NULL;
        return false;
    }
    if (HAL_TIM_IC_Start_IT(timer, TIM_CHANNEL_1) != HAL_OK) {
        (void)HAL_TIM_IC_Stop(timer, TIM_CHANNEL_2);
        s_timer = NULL;
        return false;
    }
    return true;
}

void steering_encoder_irq(void)
{
    if (s_timer == NULL) {
        return;
    }
    const uint32_t before = s_timer->Instance->SR;
    if ((before & TIM_FLAG_CC1) == 0U) {
        return;
    }
    const uint32_t period = HAL_TIM_ReadCapturedValue(s_timer, TIM_CHANNEL_1);
    const uint32_t high = HAL_TIM_ReadCapturedValue(s_timer, TIM_CHANNEL_2);
    const uint32_t after = s_timer->Instance->SR;
    __HAL_TIM_CLEAR_FLAG(s_timer, CAPTURE_FLAGS);
    const uint32_t now = HAL_GetTick();
    const bool gap = (now - s_last_edge_ms) >= STEERING_ENCODER_TIMEOUT_MS;
    s_last_edge_ms = now;

    /* A new CC1 flag after reading CCR1 means the capture changed mid-snapshot. */
    const bool overrun = (((before | after) & OVERRUN_FLAGS) != 0U) ||
                         ((after & TIM_FLAG_CC1) != 0U);
    /* Discard the first boundary after startup or lost capture. */
    if (!s_synchronized) {
        s_synchronized = !overrun;
        return;
    }
    s_capture.period_ticks = period;
    s_capture.high_ticks = high;
    s_capture.captured_at_ms = now;
    /* After a gap, wait for a complete frame to avoid accepting a wrapped period. */
    s_capture.paired = !overrun && !gap && ((before & TIM_FLAG_CC2) != 0U);
    s_capture.has_frame = true;
    if (overrun) {
        s_synchronized = false;
    }
}

steering_encoder_sample_t steering_encoder_get(void)
{
    /* Copy the ISR mailbox atomically and restore the prior IRQ state. */
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    const encoder_capture_t capture = s_capture;
    __set_PRIMASK(saved_mask);

    steering_encoder_sample_t sample = {.state = STEERING_ENCODER_WAITING};
    if (capture.has_frame) {
        const uint32_t age_ms = HAL_GetTick() - capture.captured_at_ms;
        if (age_ms >= STEERING_ENCODER_TIMEOUT_MS) {
            sample.state = STEERING_ENCODER_STALE;
        } else if (!capture.paired ||
                   !mt6701_pwm_decode(capture.period_ticks, capture.high_ticks, &sample.raw)) {
            sample.state = STEERING_ENCODER_INVALID;
        } else {
            sample.state = STEERING_ENCODER_OK;
        }
    }
    return sample;
}

const char *steering_encoder_state_name(steering_encoder_state_t state)
{
    switch (state) {
    case STEERING_ENCODER_WAITING: return "WAITING";
    case STEERING_ENCODER_OK: return "OK";
    case STEERING_ENCODER_INVALID: return "INVALID";
    case STEERING_ENCODER_STALE: return "STALE";
    default: return "UNKNOWN";
    }
}
