#undef NDEBUG
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elrs.h"
#include "pwm_rc.h"
#include "remote_uart.h"
#include "gateway_test_fakes.h"

void app_main(void);
static jmp_buf done;
static unsigned step, sends, starts, warnings;
static TickType_t now, last_warning;
static char warning[256];

const char *esp_err_to_name(esp_err_t error)
{
    return error == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}
_Noreturn void test_abort_error(esp_err_t error)
{
    (void)error;
    assert(false);
    abort();
}
void test_log(bool warn, const char *tag, const char *format, ...)
{
    (void)tag;
    if (!warn) return;
    assert(warnings == 0U || now - last_warning >= 1000U);
    last_warning = now;
    ++warnings;
    va_list args;
    va_start(args, format);
    (void)vsnprintf(warning, sizeof(warning), format, args);
    va_end(args);
}
esp_err_t elrs_start(uint16_t mask) { assert(mask == 7U); ++starts; return ESP_OK; }
esp_err_t pwm_rc_start(void) { ++starts; return ESP_OK; }
esp_err_t remote_uart_start(void) { ++starts; return ESP_OK; }
esp_err_t elrs_get_sample(elrs_sample_t *sample)
{
    *sample = (elrs_sample_t){0};
    if (step == 2U || step == 4U) {
        sample->valid = true;
        sample->normalized[1] = -1000;
        return ESP_FAIL;
    }
    return ESP_OK;
}
esp_err_t pwm_rc_get_sample(pwm_rc_sample_t *sample)
{
    *sample = (pwm_rc_sample_t){.valid = true, .valid_since_us = 1000,
                                .normalized = {0, 1000, 0}};
    return step == 1U || step == 4U ? ESP_FAIL : ESP_OK;
}
esp_err_t remote_uart_send(const remote_command_t *command)
{
    assert(remote_command_valid(command));
    assert(command->sequence == (uint8_t)step);
    if (step == 1U || step == 4U) {
        assert(command->status == REMOTE_STATUS_ERROR);
        assert(command->steering == 0 && command->throttle == 0);
    } else {
        assert(command->status == REMOTE_STATUS_OK && command->throttle == 1000);
    }
    ++sends;
    return step == 2U ? ESP_ERR_TIMEOUT : (step == 4U ? ESP_FAIL : ESP_OK);
}
TickType_t xTaskGetTickCount(void) { return now; }
BaseType_t xTaskDelayUntil(TickType_t *wake, TickType_t ticks)
{
    assert(ticks == REMOTE_SEND_PERIOD_MS);
    *wake += ticks;
    now = *wake;
    if (++step == 270U) longjmp(done, 1);
    return pdTRUE;
}
void vTaskDelay(TickType_t ticks) { now += ticks; }

int main(void)
{
    if (setjmp(done) == 0) app_main();
    assert(starts == 3U && sends == 270U);
    assert(warnings == 2U);
    assert(strstr(warning, "elrs=2 pwm=2 tx=2") != NULL);
    return 0;
}
