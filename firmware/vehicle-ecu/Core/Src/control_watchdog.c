#include "control_watchdog.h"

#include "stm32g4xx_hal.h"

/* RM0440 IWDG: enable LSI/watchdog, unlock register writes, then reload.
 * Direct access keeps ownership local without changing vendor HAL sources. */
#define IWDG_START_KEY UINT32_C(0xCCCC)
#define IWDG_WRITE_KEY UINT32_C(0x5555)
#define IWDG_RELOAD_KEY UINT32_C(0xAAAA)
#define IWDG_DIV32_BITS UINT32_C(3)
#define IWDG_WINDOW_DISABLED UINT32_C(0xFFF)
#define IWDG_UPDATE_TIMEOUT_MS UINT32_C(100)
#define IWDG_UPDATE_FLAGS (IWDG_SR_PVU | IWDG_SR_RVU | IWDG_SR_WVU)

_Static_assert(CONTROL_WATCHDOG_NOMINAL_MS > IWDG_UPDATE_TIMEOUT_MS &&
               CONTROL_WATCHDOG_NOMINAL_MS <= 4096U, "Watchdog reload bounds");

static uint32_t s_reset_flags;
static bool s_started;

static bool wait_for_registers(uint32_t started_at)
{
    while ((IWDG->SR & IWDG_UPDATE_FLAGS) != 0U) {
        if ((HAL_GetTick() - started_at) >= IWDG_UPDATE_TIMEOUT_MS) {
            return false;
        }
    }
    return true;
}

bool control_watchdog_start(void)
{
    s_started = false;
    s_reset_flags = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;
    /* Debug halt is handled by TIM1's defined LOW off-state. Freeze IWDG
     * while halted so stepping does not cause a reboot. Fault loops still
     * execute on the CPU, so the watchdog remains active in those loops. */
    DBGMCU->APB1FZR1 |= DBGMCU_APB1FZR1_DBG_IWDG_STOP;
    IWDG->KR = IWDG_START_KEY;
    IWDG->KR = IWDG_WRITE_KEY;
    IWDG->PR = IWDG_DIV32_BITS;
    IWDG->RLR = CONTROL_WATCHDOG_NOMINAL_MS - 1U;
    const uint32_t started_at = HAL_GetTick();
    if (!wait_for_registers(started_at)) {
        return false;
    }
    IWDG->WINR = IWDG_WINDOW_DISABLED;
    if (!wait_for_registers(started_at)) {
        return false;
    }
    IWDG->KR = IWDG_RELOAD_KEY;
    s_started = true;
    return true;
}

void control_watchdog_feed(void)
{
    if (s_started) {
        IWDG->KR = IWDG_RELOAD_KEY;
    }
}

bool control_watchdog_was_reset(void)
{
    return (s_reset_flags & RCC_CSR_IWDGRSTF) != 0U;
}

uint32_t control_watchdog_reset_flags(void)
{
    return s_reset_flags;
}
