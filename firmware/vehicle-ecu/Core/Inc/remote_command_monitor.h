#ifndef REMOTE_COMMAND_MONITOR_H
#define REMOTE_COMMAND_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "remote_protocol.h"

/* Maximum age of an applied command. */
#define REMOTE_COMMAND_TIMEOUT_MS UINT32_C(100)
#define REMOTE_COMMAND_FAULT_GRACE_MS UINT32_C(60)

typedef enum {
    REMOTE_COMMAND_WAITING,
    REMOTE_COMMAND_OK,
    REMOTE_COMMAND_ERROR,
    REMOTE_COMMAND_INVALID,
    REMOTE_COMMAND_STALE
} remote_command_state_t;

typedef struct {
    /* Latest valid frame, including remote ERROR status. */
    remote_command_t command;
    /* Last verified OK command used during short faults. */
    remote_command_t good_command;
    uint32_t received_at_ms;
    uint32_t good_at_ms;
    uint32_t fault_at_ms;
    uint32_t received;
    uint32_t rejected;
    uint32_t duplicates;
    bool has_command;
    bool invalid;
    bool has_good_command;
    bool fault_active;
} remote_command_monitor_t;

void remote_command_monitor_init(remote_command_monitor_t *monitor);
void remote_command_monitor_reject(remote_command_monitor_t *monitor, uint32_t received_at_ms);
/* Reject duplicate frames; only OK frames replace the applied command. */
bool remote_command_monitor_accept(remote_command_monitor_t *monitor,
                                   const remote_command_t *command, uint32_t received_at_ms);
remote_command_state_t remote_command_monitor_state(const remote_command_monitor_t *monitor,
                                                    uint32_t now_ms);
const char *remote_command_state_name(remote_command_state_t state);

#endif
