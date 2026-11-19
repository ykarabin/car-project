#include "remote_command_monitor.h"

#include <string.h>

_Static_assert(REMOTE_COMMAND_FAULT_GRACE_MS > 0U &&
               REMOTE_COMMAND_FAULT_GRACE_MS < REMOTE_COMMAND_TIMEOUT_MS, "Remote grace bound");

void remote_command_monitor_init(remote_command_monitor_t *monitor)
{
    (void)memset(monitor, 0, sizeof(*monitor));
    monitor->command.drive_mode = 1U;
    monitor->command.status = REMOTE_STATUS_ERROR;
}

static void start_fault(remote_command_monitor_t *monitor, uint32_t now_ms)
{
    if (!monitor->fault_active) {
        monitor->fault_active = true;
        monitor->fault_at_ms = now_ms;
    }
}

void remote_command_monitor_reject(remote_command_monitor_t *monitor, uint32_t received_at_ms)
{
    ++monitor->rejected;
    monitor->invalid = true;
    start_fault(monitor, received_at_ms);
}

bool remote_command_monitor_accept(remote_command_monitor_t *monitor,
                                   const remote_command_t *command,
                                   uint32_t received_at_ms)
{
    if (!remote_command_valid(command)) {
        remote_command_monitor_reject(monitor, received_at_ms);
        return false;
    }
    if (monitor->has_command && (command->sequence == monitor->command.sequence)) {
        ++monitor->duplicates;
        return false;
    }
    monitor->command = *command;
    monitor->received_at_ms = received_at_ms;
    monitor->has_command = true;
    monitor->invalid = false;
    ++monitor->received;
    if (command->status == REMOTE_STATUS_OK) {
        monitor->good_command = *command;
        monitor->good_at_ms = received_at_ms;
        monitor->has_good_command = true;
        monitor->fault_active = false;
    } else {
        start_fault(monitor, received_at_ms);
    }
    return true;
}

remote_command_state_t remote_command_monitor_state(const remote_command_monitor_t *monitor,
                                                   uint32_t now_ms)
{
    if (monitor->has_good_command &&
        ((now_ms - monitor->good_at_ms) >= REMOTE_COMMAND_TIMEOUT_MS)) {
        return REMOTE_COMMAND_STALE;
    }
    /* Invalid input never refreshes the last OK command or fault deadline. */
    if (monitor->has_good_command &&
        (!monitor->fault_active ||
         ((now_ms - monitor->fault_at_ms) < REMOTE_COMMAND_FAULT_GRACE_MS))) {
        return REMOTE_COMMAND_OK;
    }
    if (monitor->invalid) {
        return REMOTE_COMMAND_INVALID;
    }
    if (monitor->command.status == REMOTE_STATUS_ERROR) {
        return monitor->has_command ? REMOTE_COMMAND_ERROR : REMOTE_COMMAND_WAITING;
    }
    return REMOTE_COMMAND_WAITING;
}

const char *remote_command_state_name(remote_command_state_t state)
{
    switch (state) {
    case REMOTE_COMMAND_WAITING: return "WAITING";
    case REMOTE_COMMAND_OK: return "OK";
    case REMOTE_COMMAND_ERROR: return "REMOTE_ERROR";
    case REMOTE_COMMAND_INVALID: return "INVALID";
    case REMOTE_COMMAND_STALE: return "STALE";
    default: return "UNKNOWN";
    }
}
