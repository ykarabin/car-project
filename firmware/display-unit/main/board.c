#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

/* Pins from the Waveshare ESP32-S3-Touch-LCD-5 docs and demo (09_lvgl_v9_demo). */
#define PIN_I2C_SDA     GPIO_NUM_8
#define PIN_I2C_SCL     GPIO_NUM_9
#define PIN_TOUCH_INT   GPIO_NUM_4   /* held low during reset -> GT911 address 0x5D */

#define PIN_HSYNC GPIO_NUM_46
#define PIN_VSYNC GPIO_NUM_3
#define PIN_DE    GPIO_NUM_5
#define PIN_PCLK  GPIO_NUM_7

/* CH422G: writing to pseudo-address 0x24 sets the mode, 0x38 drives EXIO0..7 */
#define CH422G_ADDR_MODE 0x24
#define CH422G_ADDR_OUT  0x38
#define EXIO_TP_RST  (1 << 1)
#define EXIO_BL      (1 << 2)
#define EXIO_LCD_RST (1 << 3)
#define EXIO_SD_CS   (1 << 4)

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_ch_mode, s_ch_out;
static uint8_t s_exio = EXIO_TP_RST | EXIO_LCD_RST | EXIO_SD_CS;

static esp_err_t ch422g_write(i2c_master_dev_handle_t dev, uint8_t v)
{
    return i2c_master_transmit(dev, &v, 1, 100);
}

static esp_err_t exio_set(uint8_t bits, bool on)
{
    s_exio = on ? (s_exio | bits) : (s_exio & ~bits);
    return ch422g_write(s_ch_out, s_exio);
}

static esp_err_t i2c_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus");

    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .scl_speed_hz = 400000,
    };
    dev.device_address = CH422G_ADDR_MODE;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev, &s_ch_mode), TAG, "ch422g mode");
    dev.device_address = CH422G_ADDR_OUT;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev, &s_ch_out), TAG, "ch422g out");

    /* IO0..7 as push-pull outputs */
    return ch422g_write(s_ch_mode, 0x01);
}

static esp_err_t touch_reset(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_TOUCH_INT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "tp int");

    ESP_RETURN_ON_ERROR(exio_set(EXIO_TP_RST, false), TAG, "tp rst low");
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_TOUCH_INT, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(exio_set(EXIO_TP_RST, true), TAG, "tp rst high");
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
}

esp_err_t board_backlight(bool on)
{
    return exio_set(EXIO_BL, on);
}

esp_err_t board_init(esp_lv_adapter_tear_avoid_mode_t tear_mode,
                     esp_lcd_panel_handle_t *panel,
                     esp_lcd_touch_handle_t *touch)
{
    ESP_RETURN_ON_ERROR(i2c_init(), TAG, "i2c");
    ESP_RETURN_ON_ERROR(ch422g_write(s_ch_out, s_exio), TAG, "exio");   /* backlight off until first frame */

    const uint8_t num_fbs = esp_lv_adapter_get_required_frame_buffer_count(tear_mode, ESP_LV_ADAPTER_ROTATE_0);
    ESP_LOGI(TAG, "RGB panel %dx%d, %u frame buffers", BOARD_LCD_H_RES, BOARD_LCD_V_RES, num_fbs);

    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = CONFIG_DASH_PCLK_MHZ * 1000 * 1000,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_back_porch = 145,
            .hsync_front_porch = 170,
            .hsync_pulse_width = 30,
            .vsync_back_porch = 23,
            .vsync_front_porch = 12,
            .vsync_pulse_width = 2,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = num_fbs,
#if CONFIG_DASH_RGB_BOUNCE
        .bounce_buffer_size_px = BOARD_LCD_H_RES * 10,
#endif
        .dma_burst_size = 64,
        .hsync_gpio_num = PIN_HSYNC,
        .vsync_gpio_num = PIN_VSYNC,
        .de_gpio_num = PIN_DE,
        .pclk_gpio_num = PIN_PCLK,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums = {
            /* B3..B7, G2..G7, R3..R7 */
            GPIO_NUM_14, GPIO_NUM_38, GPIO_NUM_18, GPIO_NUM_17, GPIO_NUM_10,
            GPIO_NUM_39, GPIO_NUM_0,  GPIO_NUM_45, GPIO_NUM_48, GPIO_NUM_47, GPIO_NUM_21,
            GPIO_NUM_1,  GPIO_NUM_2,  GPIO_NUM_42, GPIO_NUM_41, GPIO_NUM_40,
        },
        .flags.fb_in_psram = 1,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, panel), TAG, "rgb panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel), TAG, "panel init");

    ESP_RETURN_ON_ERROR(touch_reset(), TAG, "touch reset");
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_bus, &tp_io_cfg, &tp_io), TAG, "tp io");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, touch), TAG, "gt911");
    return ESP_OK;
}
