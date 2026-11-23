#include <assert.h>
#include <string.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "esp_lv_adapter.h"
#include "nvs_flash.h"
#include "ui.h"

static const char *TAG = "app";

#if CONFIG_DASH_LOG_FPS
/* Per-frame profile: total render cycle, time blocked in flush (waiting for VSYNC),
 * drawing time (the rest) and invalidated pixels. Reported once per second. */
typedef struct {
    uint32_t frames, px;
    int64_t draw_us, flush_us, max_draw_us;
} prof_t;
static prof_t s_acc;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_t_render, s_t_flush, s_flush_in_frame;
static uint32_t s_px_pending;

static void prof_cb(lv_event_t *e)
{
    const int64_t now = esp_timer_get_time();
    switch (lv_event_get_code(e)) {
    case LV_EVENT_INVALIDATE_AREA: {
        const lv_area_t *a = lv_event_get_param(e);
        if (a) s_px_pending += lv_area_get_size(a);
        break;
    }
    case LV_EVENT_RENDER_START:
        s_t_render = now;
        s_flush_in_frame = 0;
        break;
    case LV_EVENT_FLUSH_START:
        s_t_flush = now;
        break;
    case LV_EVENT_FLUSH_FINISH:
        s_flush_in_frame += now - s_t_flush;
        break;
    case LV_EVENT_RENDER_READY: {
        const int64_t draw = now - s_t_render - s_flush_in_frame;
        portENTER_CRITICAL(&s_mux);
        s_acc.frames++;
        s_acc.draw_us += draw;
        s_acc.flush_us += s_flush_in_frame;
        if (draw > s_acc.max_draw_us) s_acc.max_draw_us = draw;
        s_acc.px += s_px_pending;
        portEXIT_CRITICAL(&s_mux);
        s_px_pending = 0;
        break;
    }
    default:
        break;
    }
}

static void fps_cb(void *arg)
{
    (void)arg;
    portENTER_CRITICAL(&s_mux);
    const prof_t p = s_acc;
    memset(&s_acc, 0, sizeof s_acc);
    portEXIT_CRITICAL(&s_mux);
    const uint32_t n = p.frames ? p.frames : 1;
    ESP_LOGI("fps", "%2u fps | draw avg %4.1f max %4.1f ms | flush/vsync wait %4.1f ms | %4u kpx/frame | psram free %u KiB | internal free %u KiB (min %u)",
             (unsigned)p.frames, p.draw_us / 1000.0 / n, p.max_draw_us / 1000.0, p.flush_us / 1000.0 / n,
             (unsigned)(p.px / n / 1000), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
}
#endif

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Two full frame buffers, LVGL renders straight into the back buffer (direct mode). */
    const esp_lv_adapter_tear_avoid_mode_t tear = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT;

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_touch_handle_t touch = NULL;
    ESP_ERROR_CHECK(board_init(tear, &panel, &touch));

    esp_lv_adapter_config_t acfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    acfg.task_stack_size = 16 * 1024;
    acfg.stack_in_psram = true;
#if CONFIG_DASH_LVGL_CORE1
    acfg.task_core_id = 1;   /* core 0 hosts the LCD refresh ISR (~34 % of the core) */
#endif
    ESP_ERROR_CHECK(esp_lv_adapter_init(&acfg));

    esp_lv_adapter_display_config_t dcfg = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel, NULL, BOARD_LCD_H_RES, BOARD_LCD_V_RES, ESP_LV_ADAPTER_ROTATE_0);
    dcfg.tear_avoid_mode = tear;
    dcfg.profile.use_psram = true;
    lv_display_t *disp = esp_lv_adapter_register_display(&dcfg);
    assert(disp);

    esp_lv_adapter_touch_config_t tcfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch);
    lv_indev_t *indev = esp_lv_adapter_register_touch(&tcfg);
    assert(indev);

    ESP_ERROR_CHECK(esp_lv_adapter_start());

    if (esp_lv_adapter_lock(-1) == ESP_OK) {
#if CONFIG_DASH_LOG_FPS
        lv_display_add_event_cb(disp, prof_cb, LV_EVENT_ALL, NULL);
#endif
        ui_init(disp);
        esp_lv_adapter_unlock();
    }
#if CONFIG_DASH_LOG_FPS
    const esp_timer_create_args_t targs = {.callback = fps_cb, .name = "fps"};
    esp_timer_handle_t t;
    ESP_ERROR_CHECK(esp_timer_create(&targs, &t));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t, 1000000));
#endif
    ESP_ERROR_CHECK(board_backlight(true));
    ESP_LOGI(TAG, "display unit running");
}
