#include "vehicle_app.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"
#include "remote_uart.h"
#include "vehicle_log.h"
#include "remote_command_monitor.h"
#include "vehicle_control.h"
#include "rear_encoders.h"
#include "motor_safety.h"
#include "control_watchdog.h"
#include "fault_shutdown.h"

#define STATUS_PERIOD_MS UINT32_C(500)
#define WARNING_PERIOD_MS UINT32_C(1000)

_Static_assert(pdMS_TO_TICKS(1U) > 0U, "Application period must span a tick");

void vehicle_app_run(UART_HandleTypeDef *uart)
{
    remote_command_monitor_t monitor;
    remote_command_monitor_init(&monitor);
    (void)vehicle_log_publish(&(const vehicle_log_message_t){
        .kind = VEHICLE_LOG_BOOT, .data.reset_flags = control_watchdog_reset_flags()});
    vehicle_control_init();
    if (!remote_uart_init(uart)) {
        fault_shutdown_halt();
    }
    (void)vehicle_log_publish(&(const vehicle_log_message_t){.kind = VEHICLE_LOG_CONFIG});
    (void)vehicle_log_publish(
        &(const vehicle_log_message_t){.kind = VEHICLE_LOG_REMOTE, .data.text = "WAITING"});
    uint32_t last_log = HAL_GetTick();
    uint32_t last_warning = last_log;
    steering_encoder_state_t logged_steering = STEERING_ENCODER_OK;
    wheel_speed_state_t logged_right = WHEEL_SPEED_OK;
    remote_command_state_t previous_state = REMOTE_COMMAND_WAITING;
    uint32_t logged_motor_faults = 0U;

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        remote_uart_service();
        remote_uart_event_t received;
        /* Drain the queue plus one possible overflow event. */
        for (uint32_t i = 0U; i <= REMOTE_UART_QUEUE_LENGTH; ++i) {
            if (!remote_uart_receive(&received)) {
                break;
            }
            if (received.valid) {
                (void)remote_command_monitor_accept(&monitor, &received.command,
                                                    received.received_at_ms);
            } else {
                remote_command_monitor_reject(&monitor, received.received_at_ms);
            }
        }
        const uint32_t now = HAL_GetTick();
        rear_encoders_update();
        const uint32_t motor_faults = motor_safety_poll();
        const remote_command_state_t state = remote_command_monitor_state(&monitor, now);
        const remote_command_t *control =
            ((state == REMOTE_COMMAND_OK) && (motor_faults == 0U)) ? &monitor.good_command : NULL;
        const steering_encoder_sample_t encoder = steering_encoder_get();
        const wheel_speed_sample_t rear = rear_encoders_get();
        vehicle_control_update(control, &encoder, &rear);
        const uint32_t latched_faults = motor_safety_faults();
        if (latched_faults != logged_motor_faults) {
            if (logged_motor_faults == 0U) {
                (void)vehicle_log_publish(
                    &(const vehicle_log_message_t){.kind = VEHICLE_LOG_MOTOR_FAULT_FIRST,
                                                   .data.motor_first = motor_safety_first_fault()});
            }
            logged_motor_faults = latched_faults;
            (void)vehicle_log_publish(&(const vehicle_log_message_t){
                .kind = VEHICLE_LOG_MOTOR_FAULT, .data.motor_faults = latched_faults});
        }
        if (state != previous_state) {
            (void)vehicle_log_publish(&(const vehicle_log_message_t){
                .kind = VEHICLE_LOG_REMOTE, .data.text = remote_command_state_name(state)});
            previous_state = state;
        }
        if ((now - last_log) >= STATUS_PERIOD_MS) {
            if (latched_faults != 0U) {
                (void)vehicle_log_publish(&(const vehicle_log_message_t){
                    .kind = VEHICLE_LOG_STATUS,
                    .data.status = {.remote_state = remote_command_state_name(state),
                                    .drive_mode = monitor.command.drive_mode,
                                    .motor_faults = latched_faults}});
            } else {
                vehicle_control_log_status(remote_command_state_name(state),
                                           monitor.command.drive_mode, &encoder, &rear);
            }
            last_log = now;
        }
        if ((now - last_warning) >= WARNING_PERIOD_MS) {
            remote_uart_log_errors();
            if ((encoder.state != logged_steering) || (rear.state != logged_right)) {
                const bool ready =
                    encoder.state == STEERING_ENCODER_OK && rear.state == WHEEL_SPEED_OK;
                (void)vehicle_log_publish(&(const vehicle_log_message_t){
                    .kind = VEHICLE_LOG_FEEDBACK,
                    .data.feedback = {.ready = ready,
                                      .steering = steering_encoder_state_name(encoder.state),
                                      .right = wheel_speed_state_name(rear.state)}});
                logged_steering = encoder.state;
                logged_right = rear.state;
            }
            last_warning = now;
        }
        control_watchdog_feed();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1U));
    }
}
