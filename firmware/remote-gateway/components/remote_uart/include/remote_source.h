#ifndef REMOTE_GATEWAY_REMOTE_SOURCE_H
#define REMOTE_GATEWAY_REMOTE_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { REMOTE_SOURCE_NONE = 0, REMOTE_SOURCE_ELRS, REMOTE_SOURCE_PWM } remote_source_t;

/* Keep the active source while valid; on loss, return NONE for one ERROR cycle.
 * Then select the earliest valid_since_us (esp_timer); ties prefer ELRS. */
remote_source_t remote_source_select(remote_source_t active, bool elrs_valid, int64_t elrs_since_us,
                                     bool pwm_valid, int64_t pwm_since_us);

#endif
