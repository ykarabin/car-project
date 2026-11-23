#include "vehicle_log.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "debug_console.h"
#include "control_watchdog.h"
#include "drive_controller_config.h"
#include "steering_controller_config.h"
#include "motor_safety.h"

static void format_speed(char *text, size_t size, int32_t mm_s, bool valid)
{
    if (!valid) {
        (void)snprintf(text, size, "--");
        return;
    }
    const int64_t scaled = (int64_t)mm_s * 36;
    const int32_t tenths = (int32_t)((scaled + ((scaled < 0) ? -500 : 500)) / 1000);
    const uint32_t magnitude = (uint32_t)((tenths < 0) ? -tenths : tenths);
    (void)snprintf(text, size, "%s%" PRIu32 ".%" PRIu32, (tenths < 0) ? "-" : "", magnitude / 10U,
                   magnitude % 10U);
}

static void format_status(uint32_t at_ms, const vehicle_log_status_t *status)
{
    if (status->motor_faults != 0U) {
        debug_console_printf_at(at_ms, "STAT RC=%s M=%u | MOTOR BLOCKED fault=0x%02" PRIX32,
                                status->remote_state, (unsigned)status->drive_mode,
                                status->motor_faults);
        return;
    }
    char position_text[16];
    int16_t position;
    if (status->position_valid &&
        steering_calibration_position(&status->calibration, status->raw, &position)) {
        (void)snprintf(position_text, sizeof(position_text), "%d/1000", (int)position);
    } else {
        (void)snprintf(position_text, sizeof(position_text), "--");
    }
    if (status->cal_stage != BOARD_CAL_OFF) {
        const char *step = (status->cal_stage == BOARD_CAL_CENTER) ?
                               "CENTER" :
                               ((status->cal_stage == BOARD_CAL_LEFT) ? "LEFT" : "RIGHT");
        debug_console_printf_at(at_ms, "STAT RC=%s M=%u | CAL %s pos=%s rear=OFF",
                                status->remote_state, (unsigned)status->drive_mode, step,
                                position_text);
        return;
    }
    char measured[16];
    char target[16];
    format_speed(measured, sizeof(measured), status->measured_mm_s, status->speed_valid);
    if (status->full_power) {
        (void)snprintf(target, sizeof(target), "MAX");
    } else {
        format_speed(target, sizeof(target), status->target_mm_s, true);
    }
    debug_console_printf_at(
        at_ms, "STAT RC=%s M=%u | DRIVE %s v=%skmh target=%s%s pwm=%d%% | STEER %s pos=%s pwm=%d%%",
        status->remote_state, (unsigned)status->drive_mode, status->drive_state, measured, target,
        status->full_power ? "" : "kmh", (int)status->drive_pwm / 10, status->steering_state,
        position_text, (int)status->steering_pwm / 10);
}

void vehicle_log_format(uint32_t captured_at_ms, const vehicle_log_message_t *message)
{
    if (message == NULL) {
        return;
    }
    switch (message->kind) {
    case VEHICLE_LOG_TEXT: debug_console_printf_at(captured_at_ms, "%s", message->data.text); break;
    case VEHICLE_LOG_BOOT:
        debug_console_printf_at(captured_at_ms, "BOOT ECU reset=0x%08" PRIX32 " iwdg=%" PRIu32 "ms",
                                message->data.reset_flags, CONTROL_WATCHDOG_NOMINAL_MS);
        break;
    case VEHICLE_LOG_CONFIG:
        debug_console_printf_at(
            captured_at_ms,
            "CONFIG M1=%" PRIu32 "mm/s M2=%" PRIu32 "mm/s M3=MAX steer_max=%" PRIu32 "%%",
            (uint32_t)DRIVE_CONTROL_MODE1_MAX_MM_S, (uint32_t)DRIVE_CONTROL_MODE2_MAX_MM_S,
            (uint32_t)STEERING_CONTROL_PWM_MAX / UINT32_C(10));
        break;
    case VEHICLE_LOG_CAL_STATUS:
        debug_console_printf_at(
            captured_at_ms, "CAL STATUS | mode=NORMAL saved=%u flash=%u center=%u left=%u right=%u",
            message->data.cal_status.saved ? 1U : 0U,
            message->data.cal_status.flash_available ? 1U : 0U,
            (unsigned)message->data.cal_status.calibration.center,
            (unsigned)message->data.cal_status.calibration.left,
            (unsigned)message->data.cal_status.calibration.right);
        break;
    case VEHICLE_LOG_CAL_SAVED:
        debug_console_printf_at(
            captured_at_ms,
            "CAL SAVED | center=%u left=%u right=%u; working margin=3%%; release sticks to resume",
            (unsigned)message->data.calibration.center, (unsigned)message->data.calibration.left,
            (unsigned)message->data.calibration.right);
        break;
    case VEHICLE_LOG_CAL_CENTER:
        debug_console_printf_at(captured_at_ms, "CAL CENTER | raw=%u; next: left, right",
                                (unsigned)message->data.raw);
        break;
    case VEHICLE_LOG_CAL_LEFT:
        debug_console_printf_at(captured_at_ms, "CAL LEFT | raw=%u", (unsigned)message->data.raw);
        break;
    case VEHICLE_LOG_CAL_RIGHT:
        debug_console_printf_at(captured_at_ms, "CAL RIGHT | raw=%u", (unsigned)message->data.raw);
        break;
    case VEHICLE_LOG_STEER_RELEASE:
        debug_console_printf_at(captured_at_ms, "EVENT STEER_RELEASE %s", message->data.text);
        break;
    case VEHICLE_LOG_REMOTE:
        debug_console_printf_at(captured_at_ms, "EVENT RC %s", message->data.text);
        break;
    case VEHICLE_LOG_MOTOR_FAULT_FIRST: {
        const motor_fault_snapshot_t *first = &message->data.motor_first;
        debug_console_printf_at(captured_at_ms,
                                "ERROR MOTOR_FLT_FIRST | first=0x%02" PRIX32 " slp=0x%02" PRIX32
                                " low=0x%02" PRIX32 " pending=0x%02" PRIX32 " at=%" PRIu32
                                "ms wake_age=%" PRIu32 "ms wake=0x%02" PRIX32,
                                first->first, first->slp_high, first->flt_low, first->pending,
                                first->at_ms, first->wake_age_ms, first->wake_mask);
        break;
    }
    case VEHICLE_LOG_MOTOR_FAULT: {
        const uint32_t faults = message->data.motor_faults;
        debug_console_printf_at(captured_at_ms,
                                "ERROR MOTOR_FLT LATCHED | mask=0x%02" PRIX32
                                " left=%u right=%u steering=%u | all SLP LOW; reset required",
                                faults, ((faults & MOTOR_FAULT_LEFT) != 0U) ? 1U : 0U,
                                ((faults & MOTOR_FAULT_RIGHT) != 0U) ? 1U : 0U,
                                ((faults & MOTOR_FAULT_STEERING) != 0U) ? 1U : 0U);
        break;
    }
    case VEHICLE_LOG_STATUS: format_status(captured_at_ms, &message->data.status); break;
    case VEHICLE_LOG_FEEDBACK:
        debug_console_printf_at(captured_at_ms, "%s FEEDBACK steering=%s right=%s",
                                message->data.feedback.ready ? "EVENT" : "WARN",
                                message->data.feedback.steering, message->data.feedback.right);
        break;
    case VEHICLE_LOG_UART: {
        const vehicle_log_uart_t *uart = &message->data.uart;
        debug_console_printf_at(
            captured_at_ms,
            "WARN UART hal=%" PRIu32 " ne=%" PRIu32 " ore=%" PRIu32 " fe=%" PRIu32 " pe=%" PRIu32
            " invalid=%" PRIu32 " gap=%" PRIu32 " rearm=%" PRIu32 " fifo_lost=%" PRIu32,
            uart->hal_errors, uart->noise, uart->overrun, uart->framing, uart->parity,
            uart->parse_errors, uart->partial_gap, uart->rearm_fail, uart->dropped);
        break;
    }
    default: break;
    }
}
