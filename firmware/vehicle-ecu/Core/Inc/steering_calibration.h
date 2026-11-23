#ifndef VEHICLE_STEERING_CALIBRATION_H
#define VEHICLE_STEERING_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#define STEERING_CAL_MIN_SIDE_COUNTS INT32_C(32)

#define STEERING_CAL_RECORD_WORDS 8U

typedef struct {
    uint16_t center;
    uint16_t left;
    uint16_t right;
} steering_calibration_t;

/* Raw MT6701 delta modulo 4096; calibrated travel must be less than 180 degrees. */
int32_t steering_calibration_delta(uint16_t from, uint16_t to);
/* Endpoints must be on opposite sides of center within half a turn. */
bool steering_calibration_valid(const steering_calibration_t *calibration);

/* Map raw angle to +/-1000, clamped at working limits 3% inside the captured endpoints.
 * Invalid input leaves the output unchanged. */
bool steering_calibration_position(const steering_calibration_t *calibration, uint16_t raw,
                                   int16_t *position);

/* Map +/-1000 to raw angle using asymmetric spans and the same 3% margin. */
bool steering_calibration_target(const steering_calibration_t *calibration, int16_t position,
                                 uint16_t *raw);

/* 32-byte Flash record with version, CRC-32 and a final commit doubleword. */
bool steering_calibration_encode(const steering_calibration_t *calibration, uint32_t sequence,
                                 uint32_t words[STEERING_CAL_RECORD_WORDS]);
bool steering_calibration_decode(const uint32_t words[STEERING_CAL_RECORD_WORDS],
                                 steering_calibration_t *calibration, uint32_t *sequence);
/* Compare record sequences across uint32_t wrap. */
bool steering_calibration_sequence_newer(uint32_t candidate, uint32_t previous);

#endif
