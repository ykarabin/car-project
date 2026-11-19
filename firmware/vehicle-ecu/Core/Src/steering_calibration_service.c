#include "steering_calibration_service.h"

#include <stddef.h>

#include "motor_control.h"
#include "steering_calibration_storage.h"
#include "vehicle_log.h"

#define CAL_SETTLE_MS UINT32_C(100)

void steering_calibration_service_init(steering_calibration_service_t *service,
                                       uint32_t now_ms)
{
    *service = (steering_calibration_service_t){0};
    service->has_saved = steering_calibration_storage_load(&service->saved) &&
                         steering_calibration_valid(&service->saved);
    board_controls_init(now_ms);
}

void steering_calibration_service_log_status(const steering_calibration_service_t *service)
{
    (void)vehicle_log_publish(&(const vehicle_log_message_t){
        .kind = VEHICLE_LOG_CAL_STATUS,
        .data.cal_status = {.calibration = service->saved, .saved = service->has_saved,
                            .flash_available = steering_calibration_storage_available()}
    });
    if (!service->has_saved) {
        vehicle_log_text("CAL REQUIRED | hold PC13 for calibration; rear drive available normally");
    }
}

static void update_stability(steering_calibration_service_t *service, bool sticks_neutral,
                              bool feedback_valid, uint16_t raw, uint32_t now_ms)
{
    if ((service->stage == BOARD_CAL_OFF) || !sticks_neutral || !feedback_valid) {
        service->stable = false;
        return;
    }
    const int32_t change = steering_calibration_delta(service->stable_raw, raw);
    if (!service->stable || (change < -3) || (change > 3)) {
        service->stable_raw = raw;
        service->stable_since_ms = now_ms;
        service->stable = true;
    }
}

static bool capture_ready(const steering_calibration_service_t *service, bool sticks_neutral,
                           bool feedback_valid, uint32_t now_ms)
{
    if (!sticks_neutral || !feedback_valid || !service->stable ||
        ((now_ms - service->stable_since_ms) < CAL_SETTLE_MS)) {
        vehicle_log_text("CAL ERR | release both sticks; need fresh, steady encoder for 100 ms");
        return false;
    }
    return true;
}

static void set_stage(steering_calibration_service_t *service,
                       board_cal_stage_t stage, uint32_t now_ms)
{
    service->stage = stage;
    board_controls_set_stage(stage, now_ms);
}

static steering_calibration_event_t start(steering_calibration_service_t *service,
                                           bool sticks_neutral, bool feedback_valid,
                                           uint32_t now_ms)
{
    if (!sticks_neutral || !feedback_valid) {
        vehicle_log_text("CAL ERR | calibration needs valid remote, neutral sticks and encoder OK");
        board_controls_signal(BOARD_NOTICE_ERROR, now_ms);
        return STEERING_CAL_EVENT_NONE;
    }
    motor_control_apply(NULL);
    service->draft = (steering_calibration_t){0};
    service->stable = false;
    set_stage(service, BOARD_CAL_CENTER, now_ms);
    vehicle_log_text("CAL STARTED | rear asleep, steering PWM limited to 15%; set wheels straight then confirm center");
    return STEERING_CAL_EVENT_STARTED;
}

/* Disable motors before committing the validated calibration. */
static steering_calibration_event_t save(steering_calibration_service_t *service,
                                          uint32_t now_ms)
{
    motor_control_apply(NULL);
    if (!steering_calibration_storage_save(&service->draft)) {
        vehicle_log_text("CAL ERR | Flash save failed; previous calibration retained, service remains active");
        board_controls_signal(BOARD_NOTICE_ERROR, now_ms);
        return STEERING_CAL_EVENT_NONE;
    }
    service->saved = service->draft;
    service->has_saved = true;
    set_stage(service, BOARD_CAL_OFF, now_ms);
    board_controls_signal(BOARD_NOTICE_SAVED, now_ms);
    (void)vehicle_log_publish(&(const vehicle_log_message_t){
        .kind = VEHICLE_LOG_CAL_SAVED, .data.calibration = service->saved
    });
    return STEERING_CAL_EVENT_SAVED;
}

steering_calibration_event_t steering_calibration_service_update(
    steering_calibration_service_t *service, bool sticks_neutral,
    bool feedback_valid, uint16_t raw, uint32_t now_ms)
{
    update_stability(service, sticks_neutral, feedback_valid, raw, now_ms);
    const board_button_event_t event = board_controls_poll(now_ms);
    if (event == BOARD_BUTTON_LONG) {
        if (service->stage == BOARD_CAL_OFF) {
            return start(service, sticks_neutral, feedback_valid, now_ms);
        }
        motor_control_apply(NULL);
        set_stage(service, BOARD_CAL_OFF, now_ms);
        vehicle_log_text("CAL CANCELLED | saved calibration retained; release both sticks to resume");
        return STEERING_CAL_EVENT_CANCELLED;
    }
    if ((event != BOARD_BUTTON_SHORT) || (service->stage == BOARD_CAL_OFF)) {
        return STEERING_CAL_EVENT_NONE;
    }
    if (!capture_ready(service, sticks_neutral, feedback_valid, now_ms)) {
        board_controls_signal(BOARD_NOTICE_ERROR, now_ms);
        return STEERING_CAL_EVENT_NONE;
    }
    if (service->stage == BOARD_CAL_CENTER) {
        service->draft = (steering_calibration_t){.center = raw};
        (void)vehicle_log_publish(&(const vehicle_log_message_t){
            .kind = VEHICLE_LOG_CAL_CENTER, .data.raw = service->draft.center
        });
        set_stage(service, BOARD_CAL_LEFT, now_ms);
    } else if (service->stage == BOARD_CAL_LEFT) {
        const int32_t delta = steering_calibration_delta(service->draft.center, raw);
        if ((delta > -STEERING_CAL_MIN_SIDE_COUNTS) && (delta < STEERING_CAL_MIN_SIDE_COUNTS)) {
            vehicle_log_text("CAL ERR | left endpoint too close to center; step retained");
            board_controls_signal(BOARD_NOTICE_ERROR, now_ms);
            return STEERING_CAL_EVENT_NONE;
        }
        service->draft.left = raw;
        (void)vehicle_log_publish(&(const vehicle_log_message_t){
            .kind = VEHICLE_LOG_CAL_LEFT, .data.raw = service->draft.left
        });
        set_stage(service, BOARD_CAL_RIGHT, now_ms);
    } else if (service->stage == BOARD_CAL_RIGHT) {
        steering_calibration_t candidate = service->draft;
        candidate.right = raw;
        if (!steering_calibration_valid(&candidate)) {
            set_stage(service, BOARD_CAL_LEFT, now_ms);
            vehicle_log_text("CAL ERR | invalid endpoints; capture left then right again");
            board_controls_signal(BOARD_NOTICE_ERROR, now_ms);
            return STEERING_CAL_EVENT_NONE;
        }
        service->draft = candidate;
        (void)vehicle_log_publish(&(const vehicle_log_message_t){
            .kind = VEHICLE_LOG_CAL_RIGHT, .data.raw = service->draft.right
        });
        return save(service, now_ms);
    }
    return STEERING_CAL_EVENT_NONE;
}
