#ifndef VEHICLE_STEERING_CALIBRATION_STORAGE_H
#define VEHICLE_STEERING_CALIBRATION_STORAGE_H

#include "steering_calibration.h"

bool steering_calibration_storage_available(void);
bool steering_calibration_storage_load(steering_calibration_t *calibration);

/* Task context, with all motors disabled by the caller. Erases only the
 * inactive reserved page, programs the commit last, then verifies by readback.
 * Never erases the page containing the currently selected valid record. */
bool steering_calibration_storage_save(const steering_calibration_t *calibration);

#endif
