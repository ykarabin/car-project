#include "remote_input.h"

uint8_t remote_drive_mode(int16_t channel)
{
    if (channel < -500) return 1U;
    if (channel > 500) return 3U;
    return 2U;
}

uint8_t remote_mode_cycle_update(remote_mode_cycle_t *state, bool valid,
                                 int16_t channel, int64_t received_at_us)
{
    if (state->mode == 0U) {
        state->mode = 1U;
    }
    if (!valid) {
        state->level = 0U;
        state->candidate_level = 0U;
        return state->mode;
    }

    const uint8_t level = remote_drive_mode(channel);
    if (state->level == 0U) {
        state->level = level;
        state->candidate_level = level;
        state->candidate_since_us = received_at_us;
    } else if ((level == state->level) || (level != state->candidate_level) ||
               (received_at_us < state->candidate_since_us)) {
        state->candidate_level = level;
        state->candidate_since_us = received_at_us;
    } else if ((received_at_us - state->candidate_since_us) >= REMOTE_MODE_CONFIRM_US) {
        state->level = level;
        state->mode = (uint8_t)((state->mode % 3U) + 1U);
    } else {
        /* Wait for the changed PWM level to remain stable. */
    }
    return state->mode;
}

remote_command_t remote_command_from_input(bool valid, int16_t steering, int16_t throttle,
                                           int16_t mode, uint8_t sequence)
{
    remote_command_t command = {.drive_mode = 1U, .status = REMOTE_STATUS_ERROR,
                                 .sequence = sequence};
    if (valid && (steering >= -1000) && (steering <= 1000) &&
        (throttle >= -1000) && (throttle <= 1000) && (mode >= -1000) && (mode <= 1000)) {
        command.steering = steering;
        command.throttle = throttle;
        command.drive_mode = remote_drive_mode(mode);
        command.status = REMOTE_STATUS_OK;
    }
    return command;
}
