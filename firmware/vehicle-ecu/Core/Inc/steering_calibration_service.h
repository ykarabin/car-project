#ifndef VEHICLE_STEERING_CALIBRATION_SERVICE_H
#define VEHICLE_STEERING_CALIBRATION_SERVICE_H

#include "board_controls.h"
#include "steering_calibration.h"

/* Control-task context; saved calibration and stage are read-only to callers. */
typedef struct {
    steering_calibration_t saved;
    steering_calibration_t draft;
    board_cal_stage_t stage;
    uint32_t stable_since_ms;
    uint16_t stable_raw;
    bool has_saved;
    bool stable;
} steering_calibration_service_t;

typedef enum {
    STEERING_CAL_EVENT_NONE,
    STEERING_CAL_EVENT_STARTED,
    STEERING_CAL_EVENT_CANCELLED,
    STEERING_CAL_EVENT_SAVED
} steering_calibration_event_t;

void steering_calibration_service_init(steering_calibration_service_t *service,
                                       uint32_t now_ms);
void steering_calibration_service_log_status(const steering_calibration_service_t *service);
/* Handle the button, capture steady points and save a complete calibration.
 * Stops motors before Flash writes; the caller handles the neutral wait. */
steering_calibration_event_t steering_calibration_service_update(
    steering_calibration_service_t *service, bool sticks_neutral,
    bool feedback_valid, uint16_t raw, uint32_t now_ms);

#endif
