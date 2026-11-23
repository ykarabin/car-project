#pragma once
/* Waveshare ESP32-S3-Touch-LCD-5B (SKU 28151): 5" IPS 1024x600 RGB, GT911 touch, CH422G IO expander. */

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lv_adapter.h"

#define BOARD_LCD_H_RES 1024
#define BOARD_LCD_V_RES 600

esp_err_t board_init(esp_lv_adapter_tear_avoid_mode_t tear_mode,
                     esp_lcd_panel_handle_t *panel,
                     esp_lcd_touch_handle_t *touch);

/* Backlight is a plain on/off line (CH422G EXIO2 -> AP3032 enable). */
esp_err_t board_backlight(bool on);
