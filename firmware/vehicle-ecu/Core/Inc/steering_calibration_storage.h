#ifndef VEHICLE_STEERING_CALIBRATION_STORAGE_H
#define VEHICLE_STEERING_CALIBRATION_STORAGE_H

#include "steering_calibration.h"

bool steering_calibration_storage_available(void);
bool steering_calibration_storage_load(steering_calibration_t *calibration);

/* Save to the inactive Flash page, commit last and verify; retain the previous record.
 * The caller must disable all motors first. */
bool steering_calibration_storage_save(const steering_calibration_t *calibration);

#endif
