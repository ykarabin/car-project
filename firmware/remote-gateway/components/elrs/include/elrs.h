#ifndef REMOTE_GATEWAY_ELRS_H
#define REMOTE_GATEWAY_ELRS_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define ELRS_CHANNEL_COUNT 16U

typedef struct {
    bool valid;
    int64_t received_at_us; /* Conservative reception time (esp_timer, us). */
    int64_t valid_since_us; /* Start of the current valid stream. */
    uint16_t raw[ELRS_CHANNEL_COUNT];
    int16_t normalized[ELRS_CHANNEL_COUNT];
    uint32_t rc_frames;
    uint32_t crc_errors;
    uint32_t frame_errors;
    uint32_t range_errors;
    uint32_t uart_errors;
    uint16_t rejected_raw[ELRS_CHANNEL_COUNT]; /* Last rejected channels, for diagnostics. */
} elrs_sample_t;

/* Start UART1 reception once; mask bit 0 selects CH1.
 * Only required channels determine validity. */
esp_err_t elrs_start(uint16_t required_channel_mask);

/* Thread-safe snapshot with normalized channels in +/-1000.
 * Check valid before use; invalid samples have zeroed channels and timestamps. */
esp_err_t elrs_get_sample(elrs_sample_t *sample);

#endif
