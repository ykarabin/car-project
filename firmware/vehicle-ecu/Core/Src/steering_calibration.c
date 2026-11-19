#include "steering_calibration.h"

#include <stddef.h>

#include "int32_math.h"

#define CAL_MAGIC UINT32_C(0x5343414C)
#define CAL_VERSION UINT32_C(1)
#define CAL_COMMIT UINT32_C(0x434F4D54)
#define CAL_MARGIN_PERMILLE INT32_C(30)

int32_t steering_calibration_delta(uint16_t from, uint16_t to)
{
    int32_t delta = (int32_t)to - (int32_t)from;
    if (delta >= 2048) {
        delta -= 4096;
    } else if (delta < -2048) {
        delta += 4096;
    }
    return delta;
}

bool steering_calibration_valid(const steering_calibration_t *calibration)
{
    if ((calibration == NULL) || (calibration->center >= 4096U) ||
        (calibration->left >= 4096U) || (calibration->right >= 4096U)) {
        return false;
    }
    const int32_t left = steering_calibration_delta(calibration->center, calibration->left);
    const int32_t right = steering_calibration_delta(calibration->center, calibration->right);
    return (((left < 0) && (right > 0)) || ((left > 0) && (right < 0))) &&
           (int32_magnitude(left) >= (uint32_t)STEERING_CAL_MIN_SIDE_COUNTS) &&
           (int32_magnitude(right) >= (uint32_t)STEERING_CAL_MIN_SIDE_COUNTS) &&
           ((int32_magnitude(left) + int32_magnitude(right)) < UINT32_C(2048));
}

static int32_t working_span(int32_t span)
{
    int32_t margin = (span * CAL_MARGIN_PERMILLE) / 1000;
    if (margin == 0) {
        margin = 1;
    }
    return span - margin;
}

bool steering_calibration_position(const steering_calibration_t *calibration,
                                   uint16_t raw, int16_t *position)
{
    if ((position == NULL) || (raw >= 4096U) || !steering_calibration_valid(calibration)) {
        return false;
    }
    const int32_t right_delta = steering_calibration_delta(calibration->center, calibration->right);
    int32_t value = steering_calibration_delta(calibration->center, raw);
    if (right_delta < 0) {
        value = -value;
    }
    int32_t span = (value < 0) ?
                  (int32_t)int32_magnitude(steering_calibration_delta(calibration->center, calibration->left)) :
                  (int32_t)int32_magnitude(right_delta);
    span = working_span(span);
    int32_t scaled = (value * 1000) / span;
    if (scaled > 1000) {
        scaled = 1000;
    } else if (scaled < -1000) {
        scaled = -1000;
    }
    *position = (int16_t)scaled;
    return true;
}

bool steering_calibration_target(const steering_calibration_t *calibration,
                                 int16_t position, uint16_t *raw)
{
    if ((raw == NULL) || (position < -1000) || (position > 1000) ||
        !steering_calibration_valid(calibration)) {
        return false;
    }
    const int32_t right = steering_calibration_delta(calibration->center, calibration->right);
    const int32_t side = (position < 0) ?
                         steering_calibration_delta(calibration->center, calibration->left) : right;
    const int32_t span = working_span((int32_t)int32_magnitude(side));
    int32_t offset = ((int32_t)position * span) / 1000;
    if (right < 0) {
        offset = -offset;
    }
    int32_t target = (int32_t)calibration->center + offset;
    if (target < 0) {
        target += 4096;
    } else if (target >= 4096) {
        target -= 4096;
    }
    *raw = (uint16_t)target;
    return true;
}

static uint32_t record_crc(const uint32_t *words)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0U; i < 5U; ++i) {
        uint32_t word = words[i];
        for (uint32_t byte = 0U; byte < 4U; ++byte) {
            crc ^= word & UINT32_C(0xFF);
            word >>= 8U;
            for (uint32_t bit = 0U; bit < 8U; ++bit) {
                crc = ((crc & 1U) != 0U) ?
                      ((crc >> 1U) ^ UINT32_C(0xEDB88320)) : (crc >> 1U);
            }
        }
    }
    return crc ^ UINT32_MAX;
}

bool steering_calibration_encode(const steering_calibration_t *calibration,
                                 uint32_t sequence, uint32_t words[STEERING_CAL_RECORD_WORDS])
{
    if ((words == NULL) || !steering_calibration_valid(calibration)) {
        return false;
    }
    words[0] = CAL_MAGIC;
    words[1] = CAL_VERSION;
    words[2] = sequence;
    words[3] = (uint32_t)calibration->center | ((uint32_t)calibration->left << 16U);
    words[4] = calibration->right;
    words[5] = record_crc(words);
    words[6] = CAL_COMMIT;
    words[7] = ~CAL_COMMIT;
    return true;
}

bool steering_calibration_decode(const uint32_t words[STEERING_CAL_RECORD_WORDS],
                                 steering_calibration_t *calibration, uint32_t *sequence)
{
    if ((words == NULL) || (calibration == NULL) || (sequence == NULL) ||
        (words[6] != CAL_COMMIT) || (words[7] != ~CAL_COMMIT) ||
        (words[0] != CAL_MAGIC) || (words[1] != CAL_VERSION) ||
        ((words[4] & UINT32_C(0xFFFF0000)) != 0U) || (words[5] != record_crc(words))) {
        return false;
    }
    const steering_calibration_t decoded = {
        .center = (uint16_t)(words[3] & UINT32_C(0xFFFF)),
        .left = (uint16_t)(words[3] >> 16U),
        .right = (uint16_t)words[4]
    };
    if (!steering_calibration_valid(&decoded)) {
        return false;
    }
    *calibration = decoded;
    *sequence = words[2];
    return true;
}

bool steering_calibration_sequence_newer(uint32_t candidate, uint32_t previous)
{
    const uint32_t difference = candidate - previous;
    return (difference != 0U) && (difference < UINT32_C(0x80000000));
}
