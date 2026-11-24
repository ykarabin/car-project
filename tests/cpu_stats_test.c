#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "cpu_stats.h"

#define CYCLES_PER_US 170U
#define PERIOD (1000U * CYCLES_PER_US)

static void test_loop_time_jitter_and_load(void)
{
    cpu_stats_t stats;
    cpu_stats_init(&stats, CYCLES_PER_US, PERIOD, 0U, 0U);
    uint32_t now = 0U;
    uint32_t idle = 0U;
    /* Four passes: 10, 20, 30, 40 us of work; gaps between them 1000, 1005, 997 us. */
    const uint32_t work_us[] = {10U, 20U, 30U, 40U};
    const uint32_t period_us[] = {1000U, 1005U, 997U, 1000U};
    for (size_t i = 0U; i < 4U; ++i) {
        cpu_stats_loop_begin(&stats, now);
        cpu_stats_loop_end(&stats, now + (work_us[i] * CYCLES_PER_US));
        idle += (period_us[i] - work_us[i] - 100U) * CYCLES_PER_US; /* 100 us other work */
        now += period_us[i] * CYCLES_PER_US;
    }
    const cpu_stats_snapshot_t snapshot = cpu_stats_take(&stats, now, idle);
    assert(snapshot.loops == 4U);
    assert(snapshot.loop_avg_us == 25U);
    assert(snapshot.loop_max_us == 40U);
    assert(snapshot.jitter_max_us == 5U);
    /* busy = 4 * 100 us + 100 us of work = 500 us of 4002 us -> 124.9 permille */
    assert(snapshot.cpu_load_permille == 124U);

    /* The next window starts empty; jitter continues from the last pass. */
    cpu_stats_loop_begin(&stats, now);
    cpu_stats_loop_end(&stats, now + CYCLES_PER_US);
    const cpu_stats_snapshot_t next =
        cpu_stats_take(&stats, now + PERIOD, idle + (PERIOD - CYCLES_PER_US));
    assert(next.loops == 1U && next.loop_max_us == 1U && next.loop_avg_us == 1U);
    assert(next.jitter_max_us == 0U); /* 1000 us since the last pass of the previous window */
    assert(next.cpu_load_permille == 1U);
}

static void test_counter_wrap_and_bounds(void)
{
    cpu_stats_t stats;
    const uint32_t start = UINT32_MAX - (PERIOD / 2U);
    const uint32_t idle_start = UINT32_MAX - 10U;
    cpu_stats_init(&stats, CYCLES_PER_US, PERIOD, start, idle_start);
    cpu_stats_loop_begin(&stats, start);
    cpu_stats_loop_end(&stats, start + (50U * CYCLES_PER_US)); /* wraps past zero */
    cpu_stats_loop_begin(&stats, start + PERIOD);
    cpu_stats_loop_end(&stats, start + PERIOD + (60U * CYCLES_PER_US));
    const cpu_stats_snapshot_t snapshot =
        cpu_stats_take(&stats, start + (2U * PERIOD), idle_start + PERIOD);
    assert(snapshot.loops == 2U && snapshot.loop_max_us == 60U && snapshot.loop_avg_us == 55U);
    assert(snapshot.jitter_max_us == 0U);
    assert(snapshot.cpu_load_permille == 500U);

    /* Idle above the window (counter glitch) never yields a negative load. */
    const cpu_stats_snapshot_t clamp =
        cpu_stats_take(&stats, start + (3U * PERIOD), idle_start + (3U * PERIOD));
    assert(clamp.cpu_load_permille == 0U && clamp.loops == 0U && clamp.loop_avg_us == 0U);

    /* Empty window, NULL and a zero clock rate are safe. */
    assert(cpu_stats_take(&stats, start + (3U * PERIOD), idle_start).cpu_load_permille == 0U);
    cpu_stats_loop_end(NULL, 0U);
    cpu_stats_loop_begin(NULL, 0U);
    assert(cpu_stats_take(NULL, 0U, 0U).loops == 0U);
    cpu_stats_init(&stats, 0U, PERIOD, 0U, 0U);
    cpu_stats_loop_end(&stats, 100U); /* end without begin is ignored */
    assert(cpu_stats_take(&stats, 10U, 0U).loops == 0U);
}

int main(void)
{
    test_loop_time_jitter_and_load();
    test_counter_wrap_and_bounds();
    return 0;
}
