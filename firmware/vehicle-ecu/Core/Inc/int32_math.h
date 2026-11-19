#ifndef VEHICLE_INT32_MATH_H
#define VEHICLE_INT32_MATH_H

#include <stdint.h>

/* Handle INT32_MIN without signed overflow. */
static inline uint32_t int32_magnitude(int32_t value)
{
    uint32_t result;
    if (value < 0) {
        result = (uint32_t)(-(value + INT32_C(1))) + UINT32_C(1);
    } else {
        result = (uint32_t)value;
    }
    return result;
}

#endif
