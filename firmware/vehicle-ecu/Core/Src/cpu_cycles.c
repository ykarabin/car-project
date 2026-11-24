#include "cpu_stats.h"

#include "main.h"

/* Idle accounting runs inside the scheduler's context switch; the control task reads the
 * total only while the idle task is switched out, so a plain 32-bit read is consistent. */
static volatile uint32_t s_idle_total;
static uint32_t s_idle_since;
static bool s_idle_running;

void cpu_cycles_start(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t cpu_cycles_now(void)
{
    return DWT->CYCCNT;
}

uint32_t cpu_idle_cycles(void)
{
    return s_idle_total;
}

void cpu_idle_switched_in(uint32_t is_idle)
{
    if (is_idle != 0U) {
        s_idle_since = DWT->CYCCNT;
        s_idle_running = true;
    }
}

void cpu_idle_switched_out(uint32_t is_idle)
{
    if ((is_idle != 0U) && s_idle_running) {
        s_idle_total = s_idle_total + (DWT->CYCCNT - s_idle_since);
        s_idle_running = false;
    }
}
