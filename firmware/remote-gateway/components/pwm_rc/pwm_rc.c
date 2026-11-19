#include "pwm_rc.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "driver/mcpwm_cap.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "pwm_decoder.h"
#include "sdkconfig.h"

#ifdef CONFIG_PWM_RC_STEERING_INVERTED
#define PWM_STEERING_INVERTED true
#else
#define PWM_STEERING_INVERTED false
#endif

#ifdef CONFIG_PWM_RC_THROTTLE_INVERTED
#define PWM_THROTTLE_INVERTED true
#else
#define PWM_THROTTLE_INVERTED false
#endif

static const int s_gpio[PWM_RC_CHANNEL_COUNT] = {
    CONFIG_PWM_RC_CH1_GPIO, CONFIG_PWM_RC_CH2_GPIO, CONFIG_PWM_RC_CH3_GPIO
};

static const pwm_calibration_t s_calibration[PWM_RC_CHANNEL_COUNT] = {
    {CONFIG_PWM_RC_MIN_US, CONFIG_PWM_RC_CENTER_US, CONFIG_PWM_RC_MAX_US,
     CONFIG_PWM_RC_DEADBAND_US, PWM_STEERING_INVERTED},
    {CONFIG_PWM_RC_MIN_US, CONFIG_PWM_RC_CENTER_US, CONFIG_PWM_RC_MAX_US,
     CONFIG_PWM_RC_DEADBAND_US, PWM_THROTTLE_INVERTED},
    {CONFIG_PWM_RC_MIN_US, CONFIG_PWM_RC_CENTER_US, CONFIG_PWM_RC_MAX_US, 0U, false}
};

static mcpwm_cap_timer_handle_t s_timer;
static mcpwm_cap_channel_handle_t s_channel[PWM_RC_CHANNEL_COUNT];
static size_t s_index[PWM_RC_CHANNEL_COUNT] = {0U, 1U, 2U};
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static pwm_decoder_t s_decoder;
static bool s_started;

static bool capture_edge(mcpwm_cap_channel_handle_t channel,
                         const mcpwm_capture_event_data_t *event, void *context)
{
    (void)channel;
    const size_t index = *(const size_t *)context;
    const int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&s_lock);
    pwm_decoder_capture(&s_decoder, index, event->cap_edge == MCPWM_CAP_EDGE_POS,
                         event->cap_value, now_us);
    portEXIT_CRITICAL_ISR(&s_lock);
    return false;
}

static void release_capture(const bool enabled[PWM_RC_CHANNEL_COUNT],
                             bool timer_enabled, bool timer_started)
{
    for (size_t index = 0U; index < PWM_RC_CHANNEL_COUNT; ++index) {
        if (enabled[index]) {
            ESP_ERROR_CHECK(mcpwm_capture_channel_disable(s_channel[index]));
        }
    }
    if (timer_started) {
        ESP_ERROR_CHECK(mcpwm_capture_timer_stop(s_timer));
    }
    if (timer_enabled) {
        ESP_ERROR_CHECK(mcpwm_capture_timer_disable(s_timer));
    }
    for (size_t index = 0U; index < PWM_RC_CHANNEL_COUNT; ++index) {
        if (s_channel[index] != NULL) {
            ESP_ERROR_CHECK(mcpwm_del_capture_channel(s_channel[index]));
            s_channel[index] = NULL;
        }
    }
    ESP_ERROR_CHECK(mcpwm_del_capture_timer(s_timer));
    s_timer = NULL;
}

esp_err_t pwm_rc_start(void)
{
    if (s_started || (s_timer != NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t index = 0U; index < PWM_RC_CHANNEL_COUNT; ++index) {
        if (!GPIO_IS_VALID_GPIO(s_gpio[index]) ||
            (s_gpio[index] == CONFIG_ELRS_RX_GPIO) ||
            (s_gpio[index] == CONFIG_REMOTE_UART_TX_GPIO) ||
            !pwm_calibration_valid(&s_calibration[index])) {
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t other = 0U; other < index; ++other) {
            if (s_gpio[index] == s_gpio[other]) {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    const mcpwm_capture_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT
    };
    esp_err_t error = mcpwm_new_capture_timer(&timer_config, &s_timer);
    if (error != ESP_OK) {
        return error;
    }

    bool enabled[PWM_RC_CHANNEL_COUNT] = {false};
    bool timer_enabled = false;
    bool timer_started = false;
    uint32_t resolution_hz = 0U;
    error = mcpwm_capture_timer_get_resolution(s_timer, &resolution_hz);
    if ((error == ESP_OK) && ((resolution_hz == 0U) || ((resolution_hz % 1000000U) != 0U))) {
        error = ESP_ERR_NOT_SUPPORTED;
    }
    if (error == ESP_OK) {
        s_decoder = (pwm_decoder_t){.ticks_per_us = resolution_hz / 1000000U};
        error = mcpwm_capture_timer_enable(s_timer);
        timer_enabled = (error == ESP_OK);
    }
    if (error == ESP_OK) {
        error = mcpwm_capture_timer_start(s_timer);
        timer_started = (error == ESP_OK);
    }

    const mcpwm_capture_event_callbacks_t callbacks = {.on_cap = capture_edge};
    for (size_t index = 0U; (index < PWM_RC_CHANNEL_COUNT) && (error == ESP_OK); ++index) {
        const mcpwm_capture_channel_config_t config = {
            .gpio_num = s_gpio[index],
            .prescale = 1U,
            .flags = {.pos_edge = true, .neg_edge = true}
        };
        error = mcpwm_new_capture_channel(s_timer, &config, &s_channel[index]);
        if (error == ESP_OK) {
            error = gpio_set_pull_mode(s_gpio[index], GPIO_PULLDOWN_ONLY);
        }
        if (error == ESP_OK) {
            error = mcpwm_capture_channel_register_event_callbacks(
                s_channel[index], &callbacks, &s_index[index]);
        }
        if (error == ESP_OK) {
            error = mcpwm_capture_channel_enable(s_channel[index]);
            enabled[index] = (error == ESP_OK);
        }
    }
    if (error != ESP_OK) {
        release_capture(enabled, timer_enabled, timer_started);
        return error;
    }

    s_started = true;
    return ESP_OK;
}

esp_err_t pwm_rc_get_sample(pwm_rc_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    pwm_decoder_t snapshot;
    portENTER_CRITICAL(&s_lock);
    snapshot = s_decoder;
    portEXIT_CRITICAL(&s_lock);
    pwm_decoder_sample(&snapshot, s_calibration, esp_timer_get_time(),
                        (int64_t)CONFIG_PWM_RC_TIMEOUT_MS * INT64_C(1000), sample);
    return ESP_OK;
}
