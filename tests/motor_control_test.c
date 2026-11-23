#undef NDEBUG
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "main.h"
#include "motor_config.h"
#include "motor_control.h"
#include "motor_safety.h"

GPIO_TypeDef test_gpiob;
GPIO_TypeDef test_gpiod;
TIM_TypeDef test_tim1;
RCC_TypeDef test_rcc;
DBGMCU_TypeDef test_dbgmcu;
EXTI_TypeDef test_exti;

static TIM_HandleTypeDef s_timer = {.Instance = TIM1};
static uint32_t s_started;
static uint32_t s_fail_start = UINT32_MAX;
static bool s_fail_update;
static unsigned s_updates;
static bool s_period_on_write;
static unsigned s_period_attempts;
static uint32_t s_mask;
static bool s_fault_before_wake;

static uint32_t s_now = 1000U;
uint32_t HAL_GetTick(void) { return s_now; }
static void timer_update(void);

static const uint16_t s_sleep_pins[3] = {
    LEFT_MOTOR_SLP_Pin, RIGHT_MOTOR_SLP_Pin, STEERING_MOTOR_SLP_Pin
};
static GPIO_TypeDef *const s_direction_ports[3] = {GPIOD, GPIOD, GPIOB};
static const uint16_t s_direction_pins[3] = {
    LEFT_MOTOR_DIR_Pin, RIGHT_MOTOR_DIR_Pin, STEERING_MOTOR_DIR_Pin
};
static const bool s_inverted[3] = {
    MOTOR_LEFT_DIRECTION_INVERTED != 0,
    MOTOR_RIGHT_DIRECTION_INVERTED != 0,
    MOTOR_STEERING_DIRECTION_INVERTED != 0
};

static bool awake(size_t index)
{
    return (GPIOD->ODR & s_sleep_pins[index]) != 0U;
}

uint32_t __get_PRIMASK(void) { return s_mask; }
void __disable_irq(void) { s_mask = 1U; }
void __set_PRIMASK(uint32_t mask) { s_mask = mask; }
void __DSB(void)
{
    GPIOD->ODR &= ~(GPIOD->BSRR >> 16U);
    GPIOD->BSRR = 0U;
}

void test_tim_set_compare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value)
{
    assert(timer->Instance == TIM1);
    assert((channel % 4U) == 0U && channel <= TIM_CHANNEL_3);
    assert(value <= UINT16_MAX);
    timer->Instance->compare[channel / 4U] = value;
    if (s_period_on_write) {
        ++s_period_attempts;
        timer_update();
        assert(TIM1->active_compare[0] == TIM1->active_compare[1]);
    }
}

void HAL_GPIO_WritePin(GPIO_TypeDef *gpio, uint16_t pin, GPIO_PinState state)
{
    for (size_t i = 0U; i < 3U; ++i) {
        if (gpio == s_direction_ports[i] && pin == s_direction_pins[i]) {
            const bool was_high = (gpio->ODR & pin) != 0U;
            if (was_high != (state == GPIO_PIN_SET)) {
                assert(!awake(i)); /* DIR only changes with its own bridge asleep. */
            }
        }
        if (gpio == GPIOD && (pin & s_sleep_pins[i]) != 0U &&
            state == GPIO_PIN_SET && !awake(i)) {
            /* A bridge must not wake with a stale preloaded duty. */
            assert(TIM1->active_compare[i] == TIM1->compare[i]);
            assert(s_mask == 1U && motor_safety_faults() == 0U);
        }
    }
    if (state == GPIO_PIN_SET) {
        gpio->ODR |= pin;
    } else {
        gpio->ODR &= ~(uint32_t)pin;
    }
}

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(timer->Instance == TIM1);
    assert(!awake(0U) && !awake(1U) && !awake(2U));
    if (channel == s_fail_start) {
        return HAL_ERROR;
    }
    s_started |= UINT32_C(1) << (channel / 4U);
    TIM1->BDTR |= TIM_BDTR_MOE;
    return HAL_OK;
}

static void timer_update(void)
{
    if ((TIM1->CR1 & TIM_CR1_UDIS) != 0U) return;
    for (size_t i = 0U; i < 3U; ++i) {
        TIM1->active_compare[i] = TIM1->compare[i];
    }
}

HAL_StatusTypeDef HAL_TIM_GenerateEvent(TIM_HandleTypeDef *timer, uint32_t event)
{
    assert(timer->Instance == TIM1 && event == TIM_EVENTSOURCE_UPDATE);
    assert((TIM1->CR1 & TIM_CR1_UDIS) == 0U);
    if (s_fail_update) {
        return HAL_BUSY;
    }
    timer_update();
    ++s_updates;
    if (s_fault_before_wake) {
        s_fault_before_wake = false;
        /* A confirmed burst must inhibit re-enabling after reload. */
        s_now += MOTOR_FAULT_TRANSITION_MS + 1U;
        for (uint32_t i = 0U; i < MOTOR_FAULT_PULSE_LIMIT; ++i) {
            motor_safety_fault_irq(LEFT_MOTOR_FLT_Pin);
        }
    }
    return HAL_OK;
}

static void expect_disabled(void)
{
    for (size_t i = 0U; i < 3U; ++i) {
        assert(!awake(i));
        assert(TIM1->compare[i] == 0U);
    }
}

static void init(void)
{
    s_period_on_write = false;
    s_period_attempts = 0U;
    s_mask = 0U;
    s_fault_before_wake = false;
    GPIOD->IDR = LEFT_MOTOR_FLT_Pin | RIGHT_MOTOR_FLT_Pin | STEERING_MOTOR_FLT_Pin;
    RCC->APB2ENR = RCC_APB2ENR_TIM1EN;
    motor_control_apply(NULL);
    (void)memset(TIM1, 0, sizeof(*TIM1));
    TIM1->ARR = 8499U;
    s_fail_start = UINT32_MAX;
    s_fail_update = false;
    s_started = 0U;
    s_updates = 0U;
    assert(motor_control_init(&s_timer));
    assert(s_started == 7U);
    assert((TIM1->BDTR & TIM_BDTR_OSSI) != 0U);
    assert((TIM1->CR2 & (TIM_CR2_OIS1 | TIM_CR2_OIS2 | TIM_CR2_OIS3)) == 0U);
    assert((DBGMCU->APB2FZ & DBGMCU_APB2FZ_DBG_TIM1_STOP) != 0U);
    expect_disabled();
}

static void expect_direction(size_t index, int request)
{
    assert(request != 0);
    const bool high = (s_direction_ports[index]->ODR & s_direction_pins[index]) != 0U;
    assert(high == ((request < 0) != s_inverted[index]));
}

static void test_proportional_outputs(void)
{
    init();
    static const struct {
        int16_t throttle;
        int16_t steering;
        uint32_t rear_ticks;
        uint32_t steering_ticks;
    } cases[] = {
        {0, 0, 0U, 0U},
        {500, -250, 4250U, 2125U},
        {-1000, 1000, 8500U, 8500U},
        {1, -1, 8U, 8U},
        {0, 500, 0U, 4250U},
        {-250, 0, 2125U, 0U},
        {1000, -1000, 8500U, 8500U}
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        motor_request_t command = {
            .steering_pwm = cases[i].steering,
            .rear_pwm = cases[i].throttle,
            .rear_enabled = true,
            .steering_enabled = true
        };
        motor_control_apply(&command);
        timer_update(); /* Normal CCR updates latch at the next 20 kHz period. */
        assert(awake(0U) && awake(1U) && awake(2U));
        assert(TIM1->active_compare[0] == cases[i].rear_ticks);
        assert(TIM1->active_compare[1] == cases[i].rear_ticks);
        assert(TIM1->active_compare[2] == cases[i].steering_ticks);
        if (command.rear_pwm != 0) {
            expect_direction(0U, command.rear_pwm);
            expect_direction(1U, command.rear_pwm);
        }
        if (command.steering_pwm != 0) {
            expect_direction(2U, command.steering_pwm);
        }
    }
}

static void test_disable_and_reconnect(void)
{
    init();
    motor_request_t command = {
        .rear_pwm = -1000, .steering_pwm = 500,
        .rear_enabled = true, .steering_enabled = true
    };
    motor_control_apply(&command);
    timer_update();
    motor_control_apply(NULL); /* WAITING / INVALID / STALE from the UART task. */
    expect_disabled();
    command.rear_pwm = -250; /* Same sign, new magnitude after loss of link. */
    command.steering_pwm = 100;
    motor_control_apply(&command);
    assert(TIM1->active_compare[0] == 2125U);
    assert(TIM1->active_compare[1] == 2125U);
    assert(TIM1->active_compare[2] == 850U);
    assert(awake(0U) && awake(1U) && awake(2U));

    command.rear_enabled = false;
    command.rear_pwm = 0;
    command.steering_pwm = 0;
    command.steering_enabled = false;
    motor_control_apply(&command);
    expect_disabled();
    command.rear_enabled = true;
    command.steering_enabled = true;
    motor_control_apply(&command);
    assert(awake(0U) && awake(1U) && awake(2U)); /* Valid neutral = brake. */
    assert(TIM1->active_compare[0] == 0U && TIM1->active_compare[2] == 0U);

    command.rear_pwm = INT16_MIN;
    motor_control_apply(&command);
    expect_disabled();
    command.rear_pwm = INT16_MAX;
    motor_control_apply(&command);
    expect_disabled();
    command.rear_pwm = 0;
    command.steering_pwm = 1001;
    motor_control_apply(&command);
    expect_disabled();
}

static void test_normal_updates_keep_timer_running(void)
{
    init();
    motor_request_t command = {
        .rear_pwm = 100, .steering_pwm = 100,
        .rear_enabled = true, .steering_enabled = true
    };
    motor_control_apply(&command);
    const unsigned updates = s_updates;
    command.rear_pwm = 500;
    motor_control_apply(&command);
    assert(s_updates == updates); /* No forced timer reset on ordinary updates. */
    assert(TIM1->active_compare[0] == 850U);
    timer_update();
    assert(TIM1->active_compare[0] == 4250U);
}

static void test_initialization_and_hal_failures(void)
{
    init();
    assert(!motor_control_init(NULL));
    expect_disabled();
    TIM_TypeDef other_timer = {.ARR = 8499U};
    TIM_HandleTypeDef other = {.Instance = &other_timer};
    assert(!motor_control_init(&other));
    TIM1->ARR = UINT16_MAX; /* ARR + 1 would overflow a physical 16-bit CCR. */
    assert(!motor_control_init(&s_timer));
    TIM1->ARR = 8499U;
    for (uint32_t channel = TIM_CHANNEL_1; channel <= TIM_CHANNEL_3; channel += 4U) {
        s_fail_start = channel;
        assert(!motor_control_init(&s_timer));
        expect_disabled();
        motor_request_t command = {
            .rear_pwm = 1000, .steering_pwm = 1000,
            .rear_enabled = true, .steering_enabled = true
        };
        motor_control_apply(&command);
        expect_disabled();
    }
    init();
    s_fail_update = true;
    motor_request_t command = {
        .rear_pwm = 1000, .steering_pwm = 1000,
        .rear_enabled = true, .steering_enabled = true
    };
    motor_control_apply(&command);
    expect_disabled();
    s_fail_update = false;
    motor_control_apply(&command);
    assert(awake(0U) && awake(1U) && awake(2U));
}

static void test_period_rollover_does_not_mix_rear_commands(void)
{
    init();
    motor_request_t command = {
        .rear_pwm = 100, .steering_pwm = 100,
        .rear_enabled = true, .steering_enabled = true
    };
    motor_control_apply(&command);
    timer_update();
    assert(TIM1->active_compare[0] == 850U && TIM1->active_compare[1] == 850U);
    s_period_on_write = true;
    command.rear_pwm = 700;
    command.steering_pwm = 300;
    motor_control_apply(&command);
    assert(s_period_attempts >= 3U);
    assert((TIM1->CR1 & TIM_CR1_UDIS) == 0U);
    assert(TIM1->active_compare[0] == 850U && TIM1->active_compare[1] == 850U);
    timer_update();
    assert(TIM1->active_compare[0] == 5950U && TIM1->active_compare[1] == 5950U);
    assert(TIM1->active_compare[2] == 2550U);
    s_period_on_write = false;
}

static void test_service_rear_isolation(void)
{
    init();
    motor_request_t drive = {
        .rear_pwm = 1000, .steering_pwm = 500,
        .rear_enabled = true, .steering_enabled = true
    };
    motor_control_apply(&drive);
    motor_control_apply(&(motor_request_t){.steering_pwm = -150, .steering_enabled = true});
    timer_update();
    assert(!awake(0U) && !awake(1U) && awake(2U));
    assert(TIM1->active_compare[0] == 0U && TIM1->active_compare[1] == 0U);
    assert(TIM1->active_compare[2] == 1275U);
    expect_direction(2U, -150);
    motor_control_apply(&(motor_request_t){.steering_pwm = 150, .steering_enabled = true});
    timer_update();
    assert(!awake(0U) && !awake(1U) && awake(2U));
    motor_control_apply(&(motor_request_t){.steering_pwm = 0, .steering_enabled = true});
    timer_update();
    assert(!awake(0U) && !awake(1U) && awake(2U));
    assert(TIM1->active_compare[2] == 0U);
    motor_control_apply(&drive);
    assert(awake(0U) && awake(1U) && awake(2U));
    assert(TIM1->active_compare[0] == 8500U && TIM1->active_compare[1] == 8500U);
    motor_control_apply(&(motor_request_t){.steering_pwm = 1001, .steering_enabled = true});
    expect_disabled();
}

static void test_steering_coast_is_independent_of_rear_outputs(void)
{
    init();
    motor_request_t command = {
        .rear_pwm = 500, .steering_pwm = 700,
        .rear_enabled = true, .steering_enabled = true
    };
    motor_control_apply(&command);
    timer_update();
    assert(awake(0U) && awake(1U) && awake(2U));
    assert(TIM1->active_compare[2] == 5950U);
    command.steering_enabled = false;
    motor_control_apply(&command); /* Sleep before the old PWM latches out. */
    assert(awake(0U) && awake(1U) && !awake(2U));
    assert(TIM1->compare[2] == 0U);
    timer_update();
    assert(TIM1->active_compare[0] == 4250U && TIM1->active_compare[1] == 4250U);
    assert(TIM1->active_compare[2] == 0U);

    const int16_t requests[] = {-1000, -250, 0, 250, 1000};
    for (size_t i = 0U; i < sizeof(requests) / sizeof(requests[0]); ++i) {
        command.rear_pwm = requests[i];
        motor_control_apply(&command);
        timer_update();
        assert(awake(0U) && awake(1U) && !awake(2U));
        const int32_t magnitude = requests[i] < 0 ? -(int32_t)requests[i] : requests[i];
        const uint32_t ticks = ((uint32_t)magnitude * 8500U) / 1000U;
        assert(TIM1->active_compare[0] == ticks && TIM1->active_compare[1] == ticks);
        assert(TIM1->active_compare[2] == 0U); /* Rear DIR/reload cannot wake steering. */
    }
    command.steering_pwm = 0;
    command.steering_enabled = true;
    motor_control_apply(&command);
    assert(awake(0U) && awake(1U) && awake(2U)); /* Active target still brakes at PWM=0. */
    assert(TIM1->active_compare[2] == 0U);
    command.steering_enabled = false;
    motor_control_apply(&command);
    command.steering_pwm = -500;
    command.steering_enabled = true;
    motor_control_apply(&command);
    assert(awake(2U) && TIM1->active_compare[2] == 4250U);
    expect_direction(2U, -500);
    command.rear_enabled = false;
    command.steering_enabled = false;
    motor_control_apply(&command);
    expect_disabled();
}

static void test_fault_during_reload_cannot_rewake_or_restart(void)
{
    init();
    motor_request_t drive = {
        .rear_pwm = 1000, .steering_pwm = 500,
        .rear_enabled = true, .steering_enabled = true
    };
    s_fault_before_wake = true;
    motor_control_apply(&drive);
    expect_disabled();
    assert(motor_safety_faults() == MOTOR_FAULT_LEFT);
    assert((TIM1->BDTR & TIM_BDTR_MOE) == 0U);
    motor_control_apply(&drive);
    expect_disabled();
    motor_control_apply(&(motor_request_t){.steering_pwm = 150, .steering_enabled = true});
    expect_disabled();
    assert(motor_control_init(&s_timer)); /* Reinitialization cannot clear FLT. */
    assert(motor_safety_faults() == MOTOR_FAULT_LEFT);
    motor_control_apply(&drive);
    expect_disabled();
    assert((TIM1->BDTR & TIM_BDTR_MOE) == 0U);
}

static void test_explicit_enable_masks_and_zero_initialization(void)
{
    init();
    motor_request_t request = {0};
    motor_control_apply(&request);
    expect_disabled(); /* A default request cannot wake any bridge. */
    request.rear_pwm = 500;
    request.steering_pwm = -250;
    for (unsigned mask = 0U; mask < 4U; ++mask) {
        request.rear_enabled = (mask & 1U) != 0U;
        request.steering_enabled = (mask & 2U) != 0U;
        motor_control_apply(&request);
        timer_update();
        assert(awake(0U) == request.rear_enabled && awake(1U) == request.rear_enabled);
        assert(awake(2U) == request.steering_enabled);
        assert(TIM1->active_compare[0] == (request.rear_enabled ? 4250U : 0U));
        assert(TIM1->active_compare[1] == (request.rear_enabled ? 4250U : 0U));
        assert(TIM1->active_compare[2] == (request.steering_enabled ? 2125U : 0U));
    }
    request.rear_pwm = 0;
    request.steering_pwm = 0;
    motor_control_apply(&request);
    timer_update();
    assert(awake(0U) && awake(1U) && awake(2U)); /* Enabled zero remains brake. */
    assert(TIM1->active_compare[0] == 0U && TIM1->active_compare[2] == 0U);
    const int16_t invalid[] = {INT16_MIN, -1001, 1001, INT16_MAX};
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        request.steering_pwm = invalid[i];
        motor_control_apply(&request);
        expect_disabled();
        request.steering_pwm = 0;
        request.rear_pwm = invalid[i];
        motor_control_apply(&request);
        expect_disabled();
        request.rear_pwm = 0;
    }
}

int main(void)
{
    test_proportional_outputs();
    test_disable_and_reconnect();
    test_normal_updates_keep_timer_running();
    test_initialization_and_hal_failures();
    test_period_rollover_does_not_mix_rear_commands();
    test_service_rear_isolation();
    test_steering_coast_is_independent_of_rear_outputs();
    test_explicit_enable_masks_and_zero_initialization();
    test_fault_during_reload_cannot_rewake_or_restart();
    return 0;
}
