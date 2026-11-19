#ifndef REMOTE_GATEWAY_REMOTE_SOURCE_H
#define REMOTE_GATEWAY_REMOTE_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    REMOTE_SOURCE_NONE = 0,
    REMOTE_SOURCE_ELRS,
    REMOTE_SOURCE_PWM
} remote_source_t;

/* Both timestamps use esp_timer's time base and mark the start of validity.
 * Keep a valid active source. On its loss return NONE for an ERROR cycle.
 * The following call may select the first currently valid source again.
 * An exact timestamp tie chooses ELRS deterministically.
 */
remote_source_t remote_source_select(remote_source_t active,
                                     bool elrs_valid, int64_t elrs_since_us,
                                     bool pwm_valid, int64_t pwm_since_us);

#endif
