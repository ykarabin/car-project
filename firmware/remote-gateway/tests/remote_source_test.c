#undef NDEBUG
#include <assert.h>

#include "remote_input.h"
#include "remote_source.h"

static void test_first_valid_source_and_handover(void)
{
    assert(remote_source_select(REMOTE_SOURCE_NONE, false, 0, false, 0) == REMOTE_SOURCE_NONE);
    assert(remote_source_select(REMOTE_SOURCE_NONE, true, 100, false, 0) == REMOTE_SOURCE_ELRS);
    assert(remote_source_select(REMOTE_SOURCE_NONE, false, 0, true, 100) == REMOTE_SOURCE_PWM);
    assert(remote_source_select(REMOTE_SOURCE_NONE, true, 100, true, 200) == REMOTE_SOURCE_ELRS);
    assert(remote_source_select(REMOTE_SOURCE_NONE, true, 200, true, 100) == REMOTE_SOURCE_PWM);
    assert(remote_source_select(REMOTE_SOURCE_NONE, true, 100, true, 100) == REMOTE_SOURCE_ELRS);

    assert(remote_source_select(REMOTE_SOURCE_ELRS, true, 200, true, 100) == REMOTE_SOURCE_ELRS);
    assert(remote_source_select(REMOTE_SOURCE_PWM, true, 100, true, 200) == REMOTE_SOURCE_PWM);

    /* Loss emits a neutral ERROR cycle before another source may take control. */
    remote_source_t source = remote_source_select(REMOTE_SOURCE_ELRS, false, 0, true, 200);
    assert(source == REMOTE_SOURCE_NONE);
    const remote_command_t error = remote_command_from_input(false, 800, 900, 3U, 255U);
    assert(error.status == REMOTE_STATUS_ERROR && error.steering == 0 && error.throttle == 0);
    assert(error.sequence == 255U);
    source = remote_source_select(source, false, 0, true, 200);
    assert(source == REMOTE_SOURCE_PWM);
    source = remote_source_select(source, true, 300, false, 0);
    assert(source == REMOTE_SOURCE_NONE);
    assert(remote_source_select(source, true, 300, false, 0) == REMOTE_SOURCE_ELRS);
}

static void test_two_state_mode_button(void)
{
    remote_mode_cycle_t state = {0};
    assert(remote_mode_cycle_update(&state, true, 1000, 100000) == 1U);
    assert(remote_mode_cycle_update(&state, true, 1000, 200000) == 1U);
    assert(remote_mode_cycle_update(&state, true, -1000, 220000) == 1U);
    assert(remote_mode_cycle_update(&state, true, -1000, 259999) == 1U);
    assert(remote_mode_cycle_update(&state, true, -1000, 260000) == 2U);
    assert(remote_mode_cycle_update(&state, true, -1000, 900000) == 2U);
    assert(remote_mode_cycle_update(&state, true, 1000, 920000) == 2U);
    assert(remote_mode_cycle_update(&state, true, 1000, 960000) == 3U);
    assert(remote_mode_cycle_update(&state, true, -1000, 980000) == 3U);
    assert(remote_mode_cycle_update(&state, true, -1000, 1020000) == 1U);
}

static void test_noise_duplicate_samples_and_reconnect(void)
{
    remote_mode_cycle_t state = {0};
    assert(remote_mode_cycle_update(&state, true, -1000, 100000) == 1U);
    assert(remote_mode_cycle_update(&state, true, 1000, 120000) == 1U);
    for (uint32_t repeat = 0U; repeat < 20U; ++repeat) {
        assert(remote_mode_cycle_update(&state, true, 1000, 120000) == 1U);
    }
    assert(remote_mode_cycle_update(&state, true, -1000, 140000) == 1U);
    assert(remote_mode_cycle_update(&state, true, 1000, 160000) == 1U);
    assert(remote_mode_cycle_update(&state, true, 1000, 200000) == 2U);
    assert(remote_mode_cycle_update(&state, false, 0, 0) == 2U);
    assert(remote_mode_cycle_update(&state, true, -1000, 500000) == 2U);
    assert(remote_mode_cycle_update(&state, true, -1000, 600000) == 2U);
    assert(remote_mode_cycle_update(&state, true, 1000, 620000) == 2U);
    assert(remote_mode_cycle_update(&state, true, 1000, 660000) == 3U);

    /* The existing formatter receives the same mode values for either source. */
    const remote_command_t command = remote_command_from_input(true, -500, 750, state.mode, 42U);
    assert(command.status == REMOTE_STATUS_OK && command.drive_mode == 3U);
    assert(command.steering == -500 && command.throttle == 750 && command.sequence == 42U);
}

int main(void)
{
    test_first_valid_source_and_handover();
    test_two_state_mode_button();
    test_noise_duplicate_samples_and_reconnect();
    return 0;
}
