#undef NDEBUG
#include <assert.h>
#include <stdint.h>

#include "remote_input.h"
#include "remote_command_monitor.h"

static bool deliver(remote_command_monitor_t *monitor, const remote_command_t *command, uint32_t time)
{
    char line[REMOTE_LINE_SIZE];
    const int length = remote_command_format(line, sizeof(line), command);
    assert(length > 0);
    remote_parser_t parser = {0};
    remote_command_t decoded;
    for (int i = 0; i < length; ++i) {
        const remote_parse_result_t result = remote_parser_push(&parser, (uint8_t)line[i], &decoded);
        if (result == REMOTE_PARSE_COMMAND) return remote_command_monitor_accept(monitor, &decoded, time);
        assert(result == REMOTE_PARSE_NONE);
    }
    assert(false);
    return false;
}

static void test_gateway_input_and_startup_errors(void)
{
    const int16_t mode_channels[] = {-1000, -501, -500, 0, 500, 501, 1000};
    const uint8_t modes[] = {1U, 1U, 2U, 2U, 2U, 3U, 3U};
    remote_command_monitor_t monitor;
    remote_command_monitor_init(&monitor);
    assert(remote_command_monitor_state(&monitor, 0U) == REMOTE_COMMAND_WAITING);
    remote_command_monitor_reject(&monitor, 0U);
    assert(remote_command_monitor_state(&monitor, 0U) == REMOTE_COMMAND_INVALID);
    for (size_t i = 0U; i < sizeof(modes); ++i) {
        const uint8_t mode = remote_drive_mode(mode_channels[i]);
        assert(mode == modes[i]);
        const remote_command_t command = remote_command_from_input(true, -1000, 1000,
                                                                   mode, (uint8_t)i);
        assert(command.drive_mode == modes[i] && command.status == REMOTE_STATUS_OK);
        assert(deliver(&monitor, &command, (uint32_t)i));
        assert(monitor.good_command.steering == -1000 && monitor.good_command.throttle == 1000);
    }
    const remote_command_t errors[] = {
        remote_command_from_input(false, 500, 500, 3U, 20U),
        remote_command_from_input(true, -1001, 0, 1U, 21U),
        remote_command_from_input(true, 0, 1001, 1U, 22U),
        remote_command_from_input(true, 0, 0, 0U, 23U),
        remote_command_from_input(true, 0, 0, 4U, 24U),
        remote_command_from_input(true, 0, 0, UINT8_MAX, 25U)
    };
    for (size_t i = 0U; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        remote_command_monitor_init(&monitor);
        assert(errors[i].status == REMOTE_STATUS_ERROR);
        assert(deliver(&monitor, &errors[i], 50U));
        assert(remote_command_monitor_state(&monitor, 50U) == REMOTE_COMMAND_ERROR);
        assert(!monitor.has_good_command);
    }
}

static void test_brief_rejections_keep_only_last_verified_command(void)
{
    remote_command_monitor_t monitor;
    remote_command_monitor_init(&monitor);
    remote_command_t command = remote_command_from_input(true, -321, 654, 2U, 255U);
    assert(deliver(&monitor, &command, 10U));
    remote_command_monitor_reject(&monitor, 30U);
    assert(remote_command_monitor_state(&monitor, 30U) == REMOTE_COMMAND_OK);
    assert(remote_command_monitor_state(&monitor, 89U) == REMOTE_COMMAND_OK);
    assert(monitor.good_command.throttle == 654 && monitor.good_at_ms == 10U);
    remote_command_monitor_reject(&monitor, 60U);
    assert(monitor.fault_at_ms == 30U); /* Repeated errors cannot extend grace. */
    assert(remote_command_monitor_state(&monitor, 90U) == REMOTE_COMMAND_INVALID);
    assert(!deliver(&monitor, &command, 91U)); /* Duplicate is not recovery. */
    assert(remote_command_monitor_state(&monitor, 91U) == REMOTE_COMMAND_INVALID);
    assert(monitor.duplicates == 1U && monitor.good_at_ms == 10U);
    command.sequence = 0U;
    command.throttle = -1000;
    assert(deliver(&monitor, &command, 92U));
    assert(remote_command_monitor_state(&monitor, 92U) == REMOTE_COMMAND_OK);
    assert(!monitor.fault_active && monitor.good_command.throttle == -1000);
    assert(remote_command_monitor_state(&monitor, 191U) == REMOTE_COMMAND_OK);
    assert(remote_command_monitor_state(&monitor, 192U) == REMOTE_COMMAND_STALE);
}

static void test_radio_error_grace_and_mixed_faults(void)
{
    remote_command_monitor_t monitor;
    remote_command_monitor_init(&monitor);
    remote_command_t command = remote_command_from_input(true, 500, 700, 3U, 1U);
    assert(deliver(&monitor, &command, 100U));
    command = remote_command_from_input(false, 0, 0, 1U, 2U);
    assert(deliver(&monitor, &command, 120U));
    assert(remote_command_monitor_state(&monitor, 179U) == REMOTE_COMMAND_OK);
    assert(monitor.good_command.throttle == 700 && monitor.good_command.drive_mode == 3U);
    remote_command_monitor_reject(&monitor, 140U);
    assert(monitor.fault_at_ms == 120U);
    command.sequence = 3U;
    assert(deliver(&monitor, &command, 150U));
    assert(monitor.fault_at_ms == 120U && monitor.good_at_ms == 100U);
    assert(remote_command_monitor_state(&monitor, 180U) == REMOTE_COMMAND_ERROR);
    assert(remote_command_monitor_state(&monitor, 200U) == REMOTE_COMMAND_STALE);
    command = remote_command_from_input(true, 0, 0, 2U, 0U);
    assert(deliver(&monitor, &command, 201U));
    assert(remote_command_monitor_state(&monitor, 201U) == REMOTE_COMMAND_OK);
}

static void test_timeout_wrap_and_delayed_consumer(void)
{
    remote_command_monitor_t monitor;
    remote_command_monitor_init(&monitor);
    remote_command_t command = remote_command_from_input(true, 1, -1, 2U, 1U);
    assert(deliver(&monitor, &command, UINT32_MAX - 49U));
    remote_command_monitor_reject(&monitor, UINT32_MAX - 29U);
    assert(remote_command_monitor_state(&monitor, 29U) == REMOTE_COMMAND_OK);
    assert(remote_command_monitor_state(&monitor, 30U) == REMOTE_COMMAND_INVALID);
    assert(remote_command_monitor_state(&monitor, 50U) == REMOTE_COMMAND_STALE);
    remote_command_monitor_init(&monitor);
    assert(deliver(&monitor, &command, 10U));
    remote_command_monitor_reject(&monitor, 100U);
    assert(remote_command_monitor_state(&monitor, 109U) == REMOTE_COMMAND_OK);
    assert(remote_command_monitor_state(&monitor, 110U) == REMOTE_COMMAND_STALE);
    remote_command_monitor_init(&monitor);
    assert(deliver(&monitor, &command, 100U));
    assert(remote_command_monitor_state(&monitor, 250U) == REMOTE_COMMAND_STALE);
    assert(!remote_command_monitor_accept(&monitor, NULL, 251U));
    command.throttle = 1001;
    assert(!remote_command_monitor_accept(&monitor, &command, 252U));
    assert(monitor.good_at_ms == 100U);
}

int main(void)
{
    test_gateway_input_and_startup_errors();
    test_brief_rejections_keep_only_last_verified_command();
    test_radio_error_grace_and_mixed_faults();
    test_timeout_wrap_and_delayed_consumer();
    return 0;
}
