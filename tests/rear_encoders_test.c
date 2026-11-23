#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "main.h"
#include "rear_encoders.h"
#include "rear_encoder_config.h"
#include "timebase_us.h"

TIM_TypeDef test_tim2;
TIM_TypeDef test_tim3;
static TIM_HandleTypeDef s_right = {.Instance = TIM3};
static bool s_right_started;
static bool s_fail_start;
static uint32_t s_now;

uint32_t timebase_us_now(void)
{
    return s_now * 1000U;
}

HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(channel == TIM_CHANNEL_ALL && timer->Instance == TIM3);
    if (s_fail_start) {
        return HAL_ERROR;
    }
    s_right_started = true;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_Encoder_Stop(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(channel == TIM_CHANNEL_ALL && timer->Instance == TIM3);
    s_right_started = false;
    return HAL_OK;
}

static void init(void)
{
    (void)memset(TIM2, 0, sizeof(*TIM2));
    (void)memset(TIM3, 0, sizeof(*TIM3));
    /* LEFT timer is intentionally unconfigured. It cannot prevent startup. */
    TIM2->ARR = 0U;
    TIM3->ARR = UINT16_MAX;
    s_now = 0U;
    s_fail_start = false;
    s_right_started = false;
    assert(rear_encoders_init(&s_right));
    assert(s_right_started);
    const wheel_speed_sample_t sample = rear_encoders_get();
    assert(sample.state == WHEEL_SPEED_WAITING);
}

static void test_speed_polarity_and_left_is_unused(void)
{
    init();
    /* RIGHT shaft 3000 RPM, 4096 counts/rev, one sample every 10 ms. */
    for (s_now = 10U; s_now <= 200U; s_now += 10U) {
        TIM2->CNT = UINT32_MAX - s_now; /* Unused LEFT changes cannot affect RIGHT. */
        TIM3->CNT = ((s_now / 10U) * 2048U) & UINT16_MAX;
        rear_encoders_update();
        const wheel_speed_sample_t sample = rear_encoders_get();
        assert(sample.state == WHEEL_SPEED_OK);
        assert(sample.speed_mm_s == ((REAR_ENCODER_RIGHT_INVERTED != 0) ? -2277 : 2277));
        assert(sample.interval_ms == 10U);
    }
    s_now = 210U;
    rear_encoders_update();
    assert(rear_encoders_get().speed_mm_s == 0);
    s_now = 220U;
    TIM3->CNT = (TIM3->CNT - 2048U) & UINT16_MAX;
    rear_encoders_update();
    assert(rear_encoders_get().speed_mm_s == ((REAR_ENCODER_RIGHT_INVERTED != 0) ? 2277 : -2277));
}

static void test_initialization_failures_and_inactive_reads(void)
{
    init();
    assert(!rear_encoders_init(NULL));
    rear_encoders_update();
    assert(rear_encoders_get().state == WHEEL_SPEED_WAITING);
    TIM_HandleTypeDef unused_left = {.Instance = TIM2};
    assert(!rear_encoders_init(&unused_left));
    TIM3->ARR = 100U;
    assert(!rear_encoders_init(&s_right));
    TIM3->ARR = UINT16_MAX;
    s_right_started = false;
    s_fail_start = true;
    assert(!rear_encoders_init(&s_right));
    assert(!s_right_started);
}

int main(void)
{
    test_speed_polarity_and_left_is_unused();
    test_initialization_failures_and_inactive_reads();
    return 0;
}
