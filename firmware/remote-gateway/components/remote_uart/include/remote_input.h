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
/* Zero-initialize. First valid level sets a baseline, starting in mode 1.
 * Each stable level change advances 1 -> 2 -> 3 -> 1. Signal loss keeps the
 * selected mode and clears the level baseline, avoiding a reconnect click.
 */
uint8_t remote_mode_cycle_update(remote_mode_cycle_t *state, bool valid,
                                 int16_t channel, int64_t received_at_us);
remote_command_t remote_command_from_input(bool valid, int16_t steering, int16_t throttle,
                                           int16_t mode, uint8_t sequence);

#endif
