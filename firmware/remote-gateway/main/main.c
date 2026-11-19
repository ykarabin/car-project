#include <inttypes.h>

#include "elrs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pwm_rc.h"
#include "remote_input.h"
#include "remote_source.h"
#include "remote_uart.h"
#include "sdkconfig.h"

enum {
    STEERING_CHANNEL = 0, THROTTLE_CHANNEL = 1, MODE_CHANNEL = 2,
    LOG_PERIOD_MS = 200, WARNING_PERIOD_MS = 1000
};
static const char *const TAG = "gateway";

static const char *source_name(remote_source_t source)
{
    switch (source) {
    case REMOTE_SOURCE_ELRS:
        return "ELRS";
    case REMOTE_SOURCE_PWM:
        return "PWM";
    default:
        return "NONE";
    }
}

/* Sample RC inputs and send normalized commands periodically. */
void app_main(void)
{
    const uint16_t required_channels = (uint16_t)((1U << STEERING_CHANNEL) |
                                                 (1U << THROTTLE_CHANNEL) |
                                                 (1U << MODE_CHANNEL));
    ESP_ERROR_CHECK(elrs_start(required_channels));
    ESP_ERROR_CHECK(pwm_rc_start());
    ESP_ERROR_CHECK(remote_uart_start());
    ESP_LOGI(TAG, "ELRS RX=GPIO%d; PWM CH1/2/3=GPIO%d/%d/%d; Vehicle TX=GPIO%d",
             CONFIG_ELRS_RX_GPIO, CONFIG_PWM_RC_CH1_GPIO, CONFIG_PWM_RC_CH2_GPIO,
             CONFIG_PWM_RC_CH3_GPIO, CONFIG_REMOTE_UART_TX_GPIO);
    ESP_LOGI(TAG, "Waiting for the first valid ELRS or PWM input");

    remote_source_t source = REMOTE_SOURCE_NONE;
    remote_mode_cycle_t pwm_mode = {0};
    uint8_t sequence = 0U;
    bool was_valid = false;
    TickType_t wake = xTaskGetTickCount();
    TickType_t last_log = wake;
    TickType_t last_warning = wake;
    uint32_t elrs_errors = 0U;
    uint32_t pwm_errors = 0U;
    uint32_t tx_errors = 0U;
    esp_err_t last_error = ESP_OK;
    const char *last_error_source = "NONE";
    bool error_pending = false;
    bool error_logged = false;
    for (;;) {
        elrs_sample_t elrs = {0};
        pwm_rc_sample_t pwm = {0};
        const esp_err_t elrs_error = elrs_get_sample(&elrs);
        if (elrs_error != ESP_OK) {
            elrs = (elrs_sample_t){0};
            ++elrs_errors;
            last_error = elrs_error;
            last_error_source = "ELRS";
            error_pending = true;
        }
        const esp_err_t pwm_error = pwm_rc_get_sample(&pwm);
        if (pwm_error != ESP_OK) {
            pwm = (pwm_rc_sample_t){0};
            ++pwm_errors;
            last_error = pwm_error;
            last_error_source = "PWM";
            error_pending = true;
        }
        const remote_source_t next_source = remote_source_select(
            source, elrs.valid, elrs.valid_since_us, pwm.valid, pwm.valid_since_us);
        const bool source_changed = (next_source != source);
        source = next_source;

        const uint8_t pwm_drive_mode = remote_mode_cycle_update(
            &pwm_mode, (source == REMOTE_SOURCE_PWM) && pwm.valid,
            pwm.normalized[2], pwm.channel_received_at_us[2]);
        bool input_valid = false;
        int16_t steering = 0;
        int16_t throttle = 0;
        uint8_t drive_mode = 1U;
        if (source == REMOTE_SOURCE_ELRS) {
            input_valid = elrs.valid;
            steering = elrs.normalized[STEERING_CHANNEL];
            throttle = elrs.normalized[THROTTLE_CHANNEL];
            drive_mode = remote_drive_mode(elrs.normalized[MODE_CHANNEL]);
        } else if (source == REMOTE_SOURCE_PWM) {
            input_valid = pwm.valid;
            steering = pwm.normalized[0];
            throttle = pwm.normalized[1];
            drive_mode = pwm_drive_mode;
        } else {
            /* NONE sends a neutral ERROR command. */
        }

        const remote_command_t command = remote_command_from_input(
            input_valid, steering, throttle, drive_mode, sequence);
        const esp_err_t tx_error = remote_uart_send(&command);
        if (tx_error != ESP_OK) {
            ++tx_errors;
            last_error = tx_error;
            last_error_source = "TX";
            error_pending = true;
        }
        ++sequence;

        const bool valid = (command.status == REMOTE_STATUS_OK);
        const TickType_t now = xTaskGetTickCount();
        if (source_changed || (valid != was_valid) ||
            ((now - last_log) >= pdMS_TO_TICKS(LOG_PERIOD_MS))) {
            if (source == REMOTE_SOURCE_ELRS) {
                ESP_LOGI(TAG, "RC %s src=ELRS | steering=%d throttle=%d mode=%u seq=%u | "
                         "frames=%" PRIu32 " crc_errors=%" PRIu32 " range_errors=%" PRIu32,
                         valid ? "OK" : "ERROR", (int)command.steering, (int)command.throttle,
                         (unsigned)command.drive_mode, (unsigned)command.sequence,
                         elrs.rc_frames, elrs.crc_errors, elrs.range_errors);
            } else {
                ESP_LOGI(TAG, "RC %s src=%s | steering=%d throttle=%d mode=%u seq=%u | "
                         "pwm_us=%" PRIu32 "/%" PRIu32 "/%" PRIu32 " errors=%" PRIu32
                         " elrs_crc=%" PRIu32 " range=%" PRIu32,
                         valid ? "OK" : "ERROR", source_name(source),
                         (int)command.steering, (int)command.throttle,
                         (unsigned)command.drive_mode, (unsigned)command.sequence,
                         pwm.pulse_us[0], pwm.pulse_us[1], pwm.pulse_us[2], pwm.errors,
                         elrs.crc_errors, elrs.range_errors);
            }
            last_log = now;
        }
        if (error_pending && (!error_logged ||
            ((now - last_warning) >= pdMS_TO_TICKS(WARNING_PERIOD_MS)))) {
            ESP_LOGW(TAG, "I/O last=%s:%s | elrs=%" PRIu32 " pwm=%" PRIu32 " tx=%" PRIu32,
                     last_error_source, esp_err_to_name(last_error),
                     elrs_errors, pwm_errors, tx_errors);
            error_pending = false;
            error_logged = true;
            last_warning = now;
        }
        was_valid = valid;
        if (xTaskDelayUntil(&wake, pdMS_TO_TICKS(REMOTE_SEND_PERIOD_MS)) != pdTRUE) {
            /* Restart the period after a scheduling stall. */
            vTaskDelay(pdMS_TO_TICKS(REMOTE_SEND_PERIOD_MS));
            wake = xTaskGetTickCount();
        }
    }
}
