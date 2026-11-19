#ifndef REMOTE_GATEWAY_ELRS_H
#define REMOTE_GATEWAY_ELRS_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define ELRS_CHANNEL_COUNT 16U

typedef struct {
    bool valid;
    int64_t received_at_us; /* Conservative timestamp from esp_timer_get_time(). */
    int64_t valid_since_us; /* Start of the current uninterrupted valid stream. */
    uint16_t raw[ELRS_CHANNEL_COUNT];
    int16_t normalized[ELRS_CHANNEL_COUNT];
    uint32_t rc_frames;
    uint32_t crc_errors;
    uint32_t frame_errors;
    uint32_t range_errors;
    uint32_t uart_errors;
    uint16_t rejected_raw[ELRS_CHANNEL_COUNT]; /* Last range error, diagnostic only. */
} elrs_sample_t;

/* Call once from app_main. Owns UART1 and starts the receiver task.
 * Select channels required by the application with a nonzero mask (bit 0=CH1).
 * Their range and freshness determine sample.valid. All 16 raw channels are
 * decoded for diagnostics. Unselected channel ranges do not invalidate input.
 */
esp_err_t elrs_start(uint16_t required_channel_mask);

/* Thread-safe snapshot. Invalid snapshots have zeroed channels and timestamp.
 * Channel indices are zero-based. Normalized values are in [-1000, 1000].
 * A consumer must check valid before using channels. Recheck freshness if the
 * snapshot is retained, using received_at_us and the configured timeout.
 */
esp_err_t elrs_get_sample(elrs_sample_t *sample);

#endif
