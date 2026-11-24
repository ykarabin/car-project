#ifndef VEHICLE_CPU_STATS_H
#define VEHICLE_CPU_STATS_H

#include <stdbool.h>
#include <stdint.h>

/* Control-loop timing and CPU load from a free-running cycle counter.
 * All cycle values wrap modulo 2^32 (25 s at 170 MHz); windows must be shorter. */
typedef struct {
    uint32_t loop_avg_us;
    uint32_t loop_max_us;
    uint32_t jitter_max_us; /* largest deviation of the loop period from nominal */
    uint32_t cpu_load_permille;
    uint32_t loops;
} cpu_stats_snapshot_t;

typedef struct {
    uint32_t cycles_per_us;
    uint32_t period_cycles;
    uint32_t window_start;
    uint32_t window_idle;
    uint32_t loop_start;
    uint64_t work_sum;
    uint32_t work_max;
    uint32_t jitter_max;
    uint32_t loops;
    bool has_loop;
} cpu_stats_t;

void cpu_stats_init(cpu_stats_t *stats, uint32_t cycles_per_us, uint32_t period_cycles,
                    uint32_t now, uint32_t idle_total);
/* Call at the top of each loop pass (right after the periodic wake-up). */
void cpu_stats_loop_begin(cpu_stats_t *stats, uint32_t now);
/* Call at the end of the pass, before blocking. */
void cpu_stats_loop_end(cpu_stats_t *stats, uint32_t now);
/* Return the window statistics and start a new window. */
cpu_stats_snapshot_t cpu_stats_take(cpu_stats_t *stats, uint32_t now, uint32_t idle_total);

/* DWT cycle counter and idle-time accounting (cpu_cycles.c). */
void cpu_cycles_start(void);
uint32_t cpu_cycles_now(void);
/* Total cycles spent in the FreeRTOS idle task, wraps modulo 2^32. */
uint32_t cpu_idle_cycles(void);
/* Called by the scheduler (FreeRTOSConfig.h trace macros); is_idle != 0 for the idle task. */
void cpu_idle_switched_in(uint32_t is_idle);
void cpu_idle_switched_out(uint32_t is_idle);

#endif
