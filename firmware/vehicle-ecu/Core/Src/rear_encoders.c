#include "rear_encoders.h"

#include <stddef.h>

#include "main.h"
#include "rear_encoder_config.h"
#include "timebase_us.h"

static TIM_HandleTypeDef *s_right_timer;
static wheel_speed_t s_right_speed;

static const wheel_speed_config_t s_right_config = {.counts_per_motor_rev =
                                                        REAR_ENCODER_RIGHT_COUNTS_PER_MOTOR_REV,
                                                    .sample_ms = REAR_ENCODER_RIGHT_SAMPLE_MS,
                                                    .max_gap_ms = REAR_ENCODER_RIGHT_MAX_GAP_MS,
                                                    .counter_bits = 16U,
                                                    .inverted = (REAR_ENCODER_RIGHT_INVERTED != 0)};

bool rear_encoders_init(TIM_HandleTypeDef *right)
{
    s_right_timer = NULL;
    if ((right == NULL) || (right->Instance != TIM3) ||
        (__HAL_TIM_GET_AUTORELOAD(right) != UINT16_MAX)) {
        return false;
    }
    if (HAL_TIM_Encoder_Start(right, TIM_CHANNEL_ALL) != HAL_OK) {
        return false;
    }
    const uint32_t now = timebase_us_now();
    const bool initialized =
        wheel_speed_init(&s_right_speed, &s_right_config, __HAL_TIM_GET_COUNTER(right), now);
    if (!initialized) {
        (void)HAL_TIM_Encoder_Stop(right, TIM_CHANNEL_ALL);
        return false;
    }
    s_right_timer = right;
    return true;
}

void rear_encoders_update(void)
{
    if (s_right_timer != NULL) {
        const uint32_t now = timebase_us_now();
        const uint32_t right = __HAL_TIM_GET_COUNTER(s_right_timer);
        wheel_speed_update(&s_right_speed, right, now);
    }
}

wheel_speed_sample_t rear_encoders_get(void)
{
    if (s_right_timer == NULL) {
        return (wheel_speed_sample_t){0};
    }
    return wheel_speed_get(&s_right_speed, timebase_us_now());
}
