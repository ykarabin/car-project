#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "int32_math.h"

int main(void)
{
    static const struct {
        int32_t value;
        uint32_t magnitude;
    } cases[] = {{INT32_MIN, UINT32_C(2147483648)}, {INT32_MIN + INT32_C(1), UINT32_C(2147483647)},
                 {-INT32_C(1000), UINT32_C(1000)},  {-INT32_C(1), UINT32_C(1)},
                 {INT32_C(0), UINT32_C(0)},         {INT32_C(1), UINT32_C(1)},
                 {INT32_C(1000), UINT32_C(1000)},   {INT32_MAX, UINT32_C(2147483647)}};
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        assert(int32_magnitude(cases[i].value) == cases[i].magnitude);
    }
    /* Cover every angle/error input, using a wider independent reference. */
    for (int32_t value = -4096; value <= 4096; ++value) {
        const int64_t wide = value;
        const uint32_t expected = (uint32_t)((wide < 0) ? -wide : wide);
        assert(int32_magnitude(value) == expected);
    }
    return 0;
}
