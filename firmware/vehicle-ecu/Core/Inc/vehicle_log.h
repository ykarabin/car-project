#ifndef VEHICLE_LOG_H
#define VEHICLE_LOG_H

#include <stdbool.h>
#include <stdint.h>

#include "board_controls.h"
#include "steering_calibration.h"
#include "motor_safety.h"

#define VEHICLE_LOG_QUEUE_LENGTH 16U
#define VEHICLE_LOG_STACK_WORDS 1024U

typedef struct {
    uint32_t events;
    uint32_t hal_errors;
    uint32_t overrun;
    uint32_t framing;
    uint32_t noise;
    uint32_t parity;
    uint32_t parse_errors;
    uint32_t partial_gap;
    uint32_t rearm_fail;
    uint32_t dropped;
} vehicle_log_uart_t;

typedef struct {
    const char *remote_state;
    const char *drive_state;
    const char *steering_state;
    steering_calibration_t calibration;
    int32_t measured_mm_s;
    int32_t target_mm_s;
    uint32_t motor_faults;
    int16_t drive_pwm;
    int16_t steering_pwm;
    uint16_t raw;
    uint8_t drive_mode;
    board_cal_stage_t cal_stage;
    bool position_valid;
    bool speed_valid;
    bool full_power;
} vehicle_log_status_t;

typedef enum {
    VEHICLE_LOG_TEXT,
    VEHICLE_LOG_BOOT,
    VEHICLE_LOG_CONFIG,
    VEHICLE_LOG_CAL_STATUS,
    VEHICLE_LOG_CAL_SAVED,
    VEHICLE_LOG_CAL_CENTER,
    VEHICLE_LOG_CAL_LEFT,
    VEHICLE_LOG_CAL_RIGHT,
    VEHICLE_LOG_STEER_RELEASE,
    VEHICLE_LOG_REMOTE,
    VEHICLE_LOG_MOTOR_FAULT,
    VEHICLE_LOG_MOTOR_FAULT_FIRST,
    VEHICLE_LOG_STATUS,
    VEHICLE_LOG_FEEDBACK,
    VEHICLE_LOG_UART
} vehicle_log_kind_t;

typedef struct {
    vehicle_log_kind_t kind;
    union {
        const char *text;
        uint32_t reset_flags;
        uint32_t motor_faults;
        motor_fault_snapshot_t motor_first;
        uint16_t raw;
        steering_calibration_t calibration;
        struct {
            steering_calibration_t calibration;
            bool saved;
            bool flash_available;
        } cal_status;
        struct {
            const char *steering;
            const char *right;
            bool ready;
        } feedback;
        vehicle_log_uart_t uart;
        vehicle_log_status_t status;
    } data;
} vehicle_log_message_t;

/* Start the static logger task before the scheduler; it owns RTT/printf. */
bool vehicle_log_start(void);
/* Nonblocking control-task producer; full queues drop the new record.
 * String pointers must reference immutable static storage. */
bool vehicle_log_publish(const vehicle_log_message_t *message);
void vehicle_log_text(const char *static_text);

/* Format a captured snapshot in the logger task. */
void vehicle_log_format(uint32_t captured_at_ms, const vehicle_log_message_t *message);

#endif
