#include "fault_shutdown.h"

#include "motor_safety.h"
#include "stm32g4xx_hal.h"

_Noreturn void fault_shutdown_halt(void)
{
    __disable_irq();
    motor_safety_early_off();
    for (;;) {
        __NOP();
    }
}
