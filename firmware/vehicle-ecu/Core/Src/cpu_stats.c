#include "cpu_stats.h"

#include <stddef.h>

void cpu_stats_init(cpu_stats_t *stats, uint32_t cycles_per_us, uint32_t period_cycles,
                    uint32_t now, uint32_t idle_total)
{
    if (stats != NULL) {
        *stats = (cpu_stats_t){.cycles_per_us = (cycles_per_us != 0U) ? cycles_per_us : 1U,
                               .period_cycles = period_cycles,
                               .window_start = now,
                               .window_idle = idle_total};
    }
}

void cpu_stats_loop_begin(cpu_stats_t *stats, uint32_t now)
{
    if (stats == NULL) {
        return;
    }
    if (stats->has_loop) {
        const uint32_t period = now - stats->loop_start;
        const uint32_t deviation = (period > stats->period_cycles) ?
                                       (period - stats->period_cycles) :
                                       (stats->period_cycles - period);
        if (deviation > stats->jitter_max) {
            stats->jitter_max = deviation;
        }
    }
    stats->loop_start = now;
    stats->has_loop = true;
}

void cpu_stats_loop_end(cpu_stats_t *stats, uint32_t now)
{
    if ((stats == NULL) || !stats->has_loop) {
        return;
    }
    const uint32_t work = now - stats->loop_start;
    stats->work_sum += work;
    if (work > stats->work_max) {
        stats->work_max = work;
    }
    ++stats->loops;
}

cpu_stats_snapshot_t cpu_stats_take(cpu_stats_t *stats, uint32_t now, uint32_t idle_total)
{
    cpu_stats_snapshot_t snapshot = {0};
    if (stats == NULL) {
        return snapshot;
    }
    const uint32_t window = now - stats->window_start;
    uint32_t idle = idle_total - stats->window_idle;
    if (idle > window) {
        idle = window;
    }
    snapshot.loops = stats->loops;
    snapshot.loop_max_us = stats->work_max / stats->cycles_per_us;
    if (stats->loops != 0U) {
        snapshot.loop_avg_us = (uint32_t)(stats->work_sum / stats->loops) / stats->cycles_per_us;
    }
    snapshot.jitter_max_us = stats->jitter_max / stats->cycles_per_us;
    if (window != 0U) {
        const uint64_t busy = (uint64_t)window - idle;
        snapshot.cpu_load_permille = (uint32_t)((busy * 1000U) / window);
    }
    stats->window_start = now;
    stats->window_idle = idle_total;
    stats->work_sum = 0U;
    stats->work_max = 0U;
    stats->jitter_max = 0U;
    stats->loops = 0U;
    return snapshot;
}
