#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "steering_encoder.h"

TIM_TypeDef test_tim4;
static TIM_HandleTypeDef s_timer = {.Instance = TIM4};
static uint32_t s_now;
static uint32_t s_irq_mask;
static uint32_t s_inject_flags;
static bool s_ch2_started;
static bool s_ch1_started;
static bool s_fail_start;
static bool s_fail_start_it;

uint32_t HAL_GetTick(void) { return s_now; }
uint32_t __get_PRIMASK(void) { return s_irq_mask; }
void __disable_irq(void) { s_irq_mask = 1U; }
void __set_PRIMASK(uint32_t mask) { s_irq_mask = mask; }

void test_tim_clear_flags(TIM_HandleTypeDef *timer, uint32_t flags)
{
    timer->Instance->SR &= ~flags;
}

HAL_StatusTypeDef HAL_TIM_IC_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(timer->Instance == TIM4 && channel == TIM_CHANNEL_2);
    if (s_fail_start) return HAL_ERROR;
    s_ch2_started = true;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_IC_Start_IT(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(timer->Instance == TIM4 && channel == TIM_CHANNEL_1);
    assert(s_ch2_started);
    if (s_fail_start_it) return HAL_ERROR;
    s_ch1_started = true;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_IC_Stop(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(timer->Instance == TIM4 && channel == TIM_CHANNEL_2);
    s_ch2_started = false;
    return HAL_OK;
}

uint32_t HAL_TIM_ReadCapturedValue(const TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(timer->Instance == TIM4);
    const uint32_t index = channel / 4U;
    assert(index < 2U);
    /* STM32 clears CCxIF when reading the corresponding capture register. */
    timer->Instance->SR &= ~((index == 0U) ? TIM_FLAG_CC1 : TIM_FLAG_CC2);
    if (index == 1U) {
        timer->Instance->SR |= s_inject_flags;
        s_inject_flags = 0U;
    }
    return timer->Instance->capture[index];
}

static void init(void)
{
    (void)memset(TIM4, 0, sizeof(*TIM4));
    s_now = 0U;
    s_irq_mask = 0U;
    s_inject_flags = 0U;
    s_ch2_started = false;
    s_ch1_started = false;
    s_fail_start = false;
    s_fail_start_it = false;
    assert(steering_encoder_init(&s_timer));
    assert(s_ch1_started && s_ch2_started);
    assert(steering_encoder_get().state == STEERING_ENCODER_WAITING);
}

static void rising(uint32_t now, uint32_t period, uint32_t high, uint32_t flags)
{
    s_now = now;
    TIM4->capture[0] = period;
    TIM4->capture[1] = high;
    TIM4->SR = TIM_FLAG_CC1 | flags;
    steering_encoder_irq();
}

static void test_complete_frames_and_snapshot(void)
{
    init();
    rising(0U, 123U, 12U, TIM_FLAG_CC2); /* Partial period since timer start. */
    assert(steering_encoder_get().state == STEERING_ENCODER_WAITING);
    rising(2U, 20595U, 10320U, TIM_FLAG_CC2);
    const steering_encoder_sample_t sample = steering_encoder_get();
    assert(sample.state == STEERING_ENCODER_OK);
    assert(sample.raw == 2048U);
    assert((TIM4->SR & (TIM_FLAG_CC1 | TIM_FLAG_CC2)) == 0U);
    s_irq_mask = 1U;
    assert(steering_encoder_get().state == STEERING_ENCODER_OK);
    assert(s_irq_mask == 1U); /* Must preserve an already masked caller. */
    s_irq_mask = 0U;
    (void)steering_encoder_get();
    assert(s_irq_mask == 0U);
}

static void test_timeout_and_recovery(void)
{
    init();
    rising(0U, 0U, 0U, 0U);
    rising(2U, 20595U, 80U, TIM_FLAG_CC2);
    s_now = 11U;
    assert(steering_encoder_get().state == STEERING_ENCODER_OK);
    s_now = 12U;
    assert(steering_encoder_get().state == STEERING_ENCODER_STALE);
    rising(14U, 20595U, 20555U, TIM_FLAG_CC2);
    assert(steering_encoder_get().state == STEERING_ENCODER_INVALID);
    rising(16U, 20595U, 20555U, TIM_FLAG_CC2);
    assert(steering_encoder_get().raw == 4095U);
    rising(18U, 20595U, 1U, TIM_FLAG_CC2);
    assert(steering_encoder_get().state == STEERING_ENCODER_INVALID);
    rising(20U, 20595U, 5200U, TIM_FLAG_CC2);
    assert(steering_encoder_get().state == STEERING_ENCODER_OK);
    assert(steering_encoder_get().raw == 1024U);
}

static void test_clock_wrap(void)
{
    init();
    rising(UINT32_MAX - 5U, 0U, 0U, 0U);
    rising(UINT32_MAX - 3U, 20595U, 10320U, TIM_FLAG_CC2);
    s_now = 5U;
    assert(steering_encoder_get().state == STEERING_ENCODER_OK);
    s_now = 6U;
    assert(steering_encoder_get().state == STEERING_ENCODER_STALE);
}

static void test_lost_edges_and_overcapture(void)
{
    const uint32_t overruns[] = {TIM_FLAG_CC1OF, TIM_FLAG_CC2OF};
    for (size_t i = 0U; i < sizeof(overruns) / sizeof(overruns[0]); ++i) {
        init();
        rising(0U, 0U, 0U, 0U);
        rising(2U, 20595U, 10320U, TIM_FLAG_CC2);
        rising(4U, 20595U, 5200U, TIM_FLAG_CC2 | overruns[i]);
        assert(steering_encoder_get().state == STEERING_ENCODER_INVALID);
        rising(6U, 20595U, 5200U, TIM_FLAG_CC2); /* Re-establish boundary. */
        assert(steering_encoder_get().state == STEERING_ENCODER_INVALID);
        rising(8U, 20595U, 5200U, TIM_FLAG_CC2);
        assert(steering_encoder_get().state == STEERING_ENCODER_OK);
        rising(10U, 20595U, 5200U, 0U); /* Old CCR2 without a new falling edge. */
        assert(steering_encoder_get().state == STEERING_ENCODER_INVALID);
    }
}

static void test_edges_during_snapshot(void)
{
    const uint32_t inject[] = {TIM_FLAG_CC1, TIM_FLAG_CC2OF};
    for (size_t i = 0U; i < sizeof(inject) / sizeof(inject[0]); ++i) {
        init();
        rising(0U, 0U, 0U, 0U);
        s_inject_flags = inject[i];
        rising(2U, 20595U, 10320U, TIM_FLAG_CC2);
        assert(steering_encoder_get().state == STEERING_ENCODER_INVALID);
        rising(4U, 20595U, 5200U, TIM_FLAG_CC2);
        assert(steering_encoder_get().state == STEERING_ENCODER_INVALID);
        rising(6U, 20595U, 5200U, TIM_FLAG_CC2);
        assert(steering_encoder_get().state == STEERING_ENCODER_OK);
        assert(steering_encoder_get().raw == 1024U);
    }
}

static void test_start_failures(void)
{
    init();
    assert(!steering_encoder_init(NULL));
    TIM_TypeDef other_instance = {0};
    TIM_HandleTypeDef other = {.Instance = &other_instance};
    assert(!steering_encoder_init(&other));
    s_fail_start = true;
    assert(!steering_encoder_init(&s_timer));
    s_fail_start = false;
    s_fail_start_it = true;
    assert(!steering_encoder_init(&s_timer));
    assert(!s_ch2_started);
    rising(2U, 20595U, 10320U, TIM_FLAG_CC2);
    assert(steering_encoder_get().state == STEERING_ENCODER_WAITING);
}

int main(void)
{
    test_complete_frames_and_snapshot();
    test_timeout_and_recovery();
    test_clock_wrap();
    test_lost_edges_and_overcapture();
    test_edges_during_snapshot();
    test_start_failures();
    return 0;
}
