#include "motor_control.h"

#include <stddef.h>
#include <stdint.h>

#include "main.h"
#include "motor_config.h"
#include "motor_safety.h"

#define MOTOR_COUNT 3U
#define REAR_MOTOR_COUNT 2U
#define MOTOR_COMMAND_SCALE UINT32_C(1000)

typedef struct {
    GPIO_TypeDef *direction_port;
    uint16_t direction_pin;
    GPIO_TypeDef *sleep_port;
    uint16_t sleep_pin;
    uint32_t pwm_channel;
    bool direction_inverted;
} motor_output_t;

/* Motor order: left rear, right rear, steering. */
static const motor_output_t s_motors[MOTOR_COUNT] = {
    {.direction_port = LEFT_MOTOR_DIR_GPIO_Port,
     .direction_pin = LEFT_MOTOR_DIR_Pin,
     .sleep_port = LEFT_MOTOR_SLP_GPIO_Port,
     .sleep_pin = LEFT_MOTOR_SLP_Pin,
     .pwm_channel = TIM_CHANNEL_1,
     .direction_inverted = (MOTOR_LEFT_DIRECTION_INVERTED != 0)},
    {.direction_port = RIGHT_MOTOR_DIR_GPIO_Port,
     .direction_pin = RIGHT_MOTOR_DIR_Pin,
     .sleep_port = RIGHT_MOTOR_SLP_GPIO_Port,
     .sleep_pin = RIGHT_MOTOR_SLP_Pin,
     .pwm_channel = TIM_CHANNEL_2,
     .direction_inverted = (MOTOR_RIGHT_DIRECTION_INVERTED != 0)},
    {.direction_port = STEERING_MOTOR_DIR_GPIO_Port,
     .direction_pin = STEERING_MOTOR_DIR_Pin,
     .sleep_port = STEERING_MOTOR_SLP_GPIO_Port,
     .sleep_pin = STEERING_MOTOR_SLP_Pin,
     .pwm_channel = TIM_CHANNEL_3,
     .direction_inverted = (MOTOR_STEERING_DIRECTION_INVERTED != 0)}};

static TIM_HandleTypeDef *s_timer;
static uint32_t s_period_ticks;
static GPIO_PinState s_direction[MOTOR_COUNT];
static bool s_enabled[MOTOR_COUNT];

static void disable_outputs(void)
{
    for (size_t i = 0U; i < MOTOR_COUNT; ++i) {
        const motor_output_t *motor = &s_motors[i];
        HAL_GPIO_WritePin(motor->sleep_port, motor->sleep_pin, GPIO_PIN_RESET);
        if (s_timer != NULL) {
            __HAL_TIM_SET_COMPARE(s_timer, motor->pwm_channel, 0U);
        }
        s_enabled[i] = false;
    }
}

bool motor_control_init(TIM_HandleTypeDef *pwm_timer)
{
    disable_outputs();
    s_timer = NULL;
    if ((pwm_timer == NULL) || (pwm_timer->Instance != TIM1)) {
        return false;
    }

    const uint32_t auto_reload = __HAL_TIM_GET_AUTORELOAD(pwm_timer);
    /* ARR + 1 must fit the 16-bit CCR for 100% duty. */
    if ((auto_reload == 0U) || (auto_reload >= UINT16_MAX)) {
        return false;
    }
    s_timer = pwm_timer;
    s_period_ticks = auto_reload + 1U;

    /* Drive PWM LOW while the debugger halts TIM1. */
    TIM1->CR2 &= ~(TIM_CR2_OIS1 | TIM_CR2_OIS2 | TIM_CR2_OIS3);
    TIM1->BDTR |= TIM_BDTR_OSSI;
    DBGMCU->APB2FZ |= DBGMCU_APB2FZ_DBG_TIM1_STOP;

    for (size_t i = 0U; i < MOTOR_COUNT; ++i) {
        const motor_output_t *motor = &s_motors[i];
        __HAL_TIM_SET_COMPARE(s_timer, motor->pwm_channel, 0U);
        HAL_GPIO_WritePin(motor->direction_port, motor->direction_pin, GPIO_PIN_RESET);
        s_direction[i] = GPIO_PIN_RESET;
        if (HAL_TIM_PWM_Start(s_timer, motor->pwm_channel) != HAL_OK) {
            disable_outputs();
            s_timer = NULL;
            return false;
        }
    }
    (void)motor_safety_poll();
    return true;
}

static void apply_outputs(int16_t throttle, int16_t steering, bool rear_enabled,
                          bool steering_enabled)
{
    if ((s_timer == NULL) || (motor_safety_poll() != 0U)) {
        disable_outputs();
        return;
    }

    bool reload_before_enable = false;
    /* Batch CCR writes so both rear channels update together. */
    s_timer->Instance->CR1 |= TIM_CR1_UDIS;
    for (size_t i = 0U; i < MOTOR_COUNT; ++i) {
        const motor_output_t *motor = &s_motors[i];
        const bool enabled = (i < REAR_MOTOR_COUNT) ? rear_enabled : steering_enabled;
        if (!enabled) {
            HAL_GPIO_WritePin(motor->sleep_port, motor->sleep_pin, GPIO_PIN_RESET);
            __HAL_TIM_SET_COMPARE(s_timer, motor->pwm_channel, 0U);
            s_enabled[i] = false;
            continue;
        }
        if (!s_enabled[i]) {
            reload_before_enable = true;
        }
        const int32_t request = (i < REAR_MOTOR_COUNT) ? (int32_t)throttle : (int32_t)steering;
        if (request != 0) {
            const bool reverse = (request < 0) != motor->direction_inverted;
            const GPIO_PinState direction = reverse ? GPIO_PIN_SET : GPIO_PIN_RESET;
            if (direction != s_direction[i]) {
                /* Put the bridge to sleep before changing DIR. */
                HAL_GPIO_WritePin(motor->sleep_port, motor->sleep_pin, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(motor->direction_port, motor->direction_pin, direction);
                s_direction[i] = direction;
                reload_before_enable = true;
            }
        }
        const uint32_t magnitude = (request < 0) ? (uint32_t)(-request) : (uint32_t)request;
        const uint32_t compare = (magnitude * s_period_ticks) / MOTOR_COMMAND_SCALE;
        __HAL_TIM_SET_COMPARE(s_timer, motor->pwm_channel, compare);
    }
    s_timer->Instance->CR1 &= ~TIM_CR1_UDIS;

    if (reload_before_enable) {
        /* Latch new duty before waking a bridge to avoid replaying an old command. */
        if (HAL_TIM_GenerateEvent(s_timer, TIM_EVENTSOURCE_UPDATE) != HAL_OK) {
            disable_outputs();
            return;
        }
        if (!motor_safety_wake(rear_enabled, steering_enabled)) {
            disable_outputs();
            return;
        }
        for (size_t i = 0U; i < MOTOR_COUNT; ++i) {
            const bool enabled = (i < REAR_MOTOR_COUNT) ? rear_enabled : steering_enabled;
            if (enabled) {
                s_enabled[i] = true;
            }
        }
    }
}

void motor_control_apply(const motor_request_t *request)
{
    if ((request == NULL) || (request->rear_pwm < -1000) || (request->rear_pwm > 1000) ||
        (request->steering_pwm < -1000) || (request->steering_pwm > 1000)) {
        disable_outputs();
        return;
    }
    apply_outputs(request->rear_pwm, request->steering_pwm, request->rear_enabled,
                  request->steering_enabled);
}
