#ifndef VEHICLE_FAULT_SHUTDOWN_H
#define VEHICLE_FAULT_SHUTDOWN_H

/* Fatal MCU/RTOS path: disable motor outputs and halt with IRQs masked.
 * Works without initialized HAL handles, RTOS or logging. */
_Noreturn void fault_shutdown_halt(void);

#endif
