#ifndef VEHICLE_FAULT_SHUTDOWN_H
#define VEHICLE_FAULT_SHUTDOWN_H

/* Terminal MCU/RTOS fault path. Drops all motor SLP outputs, disables TIM1
 * PWM and halts with interrupts masked. No HAL handles, tick, RTOS or logs.
 * Does not return or permit motor restart. IWDG may reset the MCU; that
 * reboot requires a valid neutral remote command before enabling outputs. */
_Noreturn void fault_shutdown_halt(void);

#endif
