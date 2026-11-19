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

/* Raw MT6701 coordinates, modulo 4096. Sensor travel must be <180 degrees. */
int32_t steering_calibration_delta(uint16_t from, uint16_t to);
bool steering_calibration_valid(const steering_calibration_t *calibration);

/* Relative position -1000 left .. 0 center .. +1000 right. Each working
 * endpoint is 3% inside the captured center-to-end travel. Saturates outside
 * those endpoints. Output is unchanged on invalid input. */
bool steering_calibration_position(const steering_calibration_t *calibration,
                                   uint16_t raw, int16_t *position);

/* Convert stick position +/-1000 to an absolute raw working target. Uses the
 * same asymmetric ranges and 3% endpoint margin as position(). */
bool steering_calibration_target(const steering_calibration_t *calibration,
                                 int16_t position, uint16_t *raw);

/* Versioned 32-byte Flash record: little-endian words, CRC-32/ISO-HDLC,
 * final doubleword is the commit marker. No native-struct serialization. */
bool steering_calibration_encode(const steering_calibration_t *calibration,
                                 uint32_t sequence, uint32_t words[STEERING_CAL_RECORD_WORDS]);
bool steering_calibration_decode(const uint32_t words[STEERING_CAL_RECORD_WORDS],
                                 steering_calibration_t *calibration, uint32_t *sequence);
bool steering_calibration_sequence_newer(uint32_t candidate, uint32_t previous);

#endif
