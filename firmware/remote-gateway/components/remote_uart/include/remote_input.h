#ifndef REMOTE_INPUT_H
#define REMOTE_INPUT_H

#include "remote_protocol.h"

#define REMOTE_MODE_CONFIRM_US INT64_C(40000)

typedef struct {
    uint8_t mode;
    uint8_t level;
    uint8_t candidate_level;
    int64_t candidate_since_us;
} remote_mode_cycle_t;

uint8_t remote_drive_mode(int16_t channel);
/* Zero-initialize. Stable level changes cycle 1 -> 2 -> 3.
 * Signal loss clears the level baseline but keeps the selected mode. */
uint8_t remote_mode_cycle_update(remote_mode_cycle_t *state, bool valid, int16_t channel,
                                 int64_t received_at_us);
/* Build a command from normalized sticks and a decoded mode 1..3. */
remote_command_t remote_command_from_input(bool valid, int16_t steering, int16_t throttle,
                                           uint8_t drive_mode, uint8_t sequence);

#endif
