#pragma once
/* host stub: no persistent storage, defaults are used */
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
typedef uint32_t nvs_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define NVS_READONLY 0
#define NVS_READWRITE 1
static inline esp_err_t nvs_open(const char *n, int m, nvs_handle_t *h) { (void)n; (void)m; (void)h; return ESP_FAIL; }
static inline esp_err_t nvs_get_u32(nvs_handle_t h, const char *k, uint32_t *v) { (void)h; (void)k; (void)v; return ESP_FAIL; }
static inline esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) { (void)h; (void)k; (void)v; return ESP_FAIL; }
static inline esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *v, size_t *n) { (void)h; (void)k; (void)v; (void)n; return ESP_FAIL; }
static inline esp_err_t nvs_set_u32(nvs_handle_t h, const char *k, uint32_t v) { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *v) { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
static inline void nvs_close(nvs_handle_t h) { (void)h; }
