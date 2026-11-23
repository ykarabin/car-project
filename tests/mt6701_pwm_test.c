#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "mt6701_pwm.h"

static void test_fixed_angles(void)
{
    static const struct {
        uint32_t high;
        uint16_t raw;
    } cases[] = {{80U, 0U},       {85U, 1U},       {5200U, 1024U},
                 {10320U, 2048U}, {15440U, 3072U}, {20555U, 4095U}};
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint16_t raw;
        assert(mt6701_pwm_decode(20595U, cases[i].high, &raw));
        assert(raw == cases[i].raw);
    }
}

static void test_all_positions_at_both_rates(void)
{
    /* Include the observed 501.5 Hz and both nominal frame-rate windows.
     * Timer quantization must not lose the first/last position. */
    const uint32_t periods[] = {9000U, 10057U, 11000U, 18000U, 19940U, 20112U, 22000U};
    for (size_t i = 0U; i < sizeof(periods) / sizeof(periods[0]); ++i) {
        for (uint32_t expected_raw = 0U; expected_raw < 4096U; ++expected_raw) {
            const uint32_t high = (((expected_raw + 16U) * periods[i]) + 2059U) / 4119U;
            uint16_t raw;
            assert(mt6701_pwm_decode(periods[i], high, &raw));
            assert(raw == expected_raw);
        }
    }
}

static void test_invalid_frames_leave_output_unchanged(void)
{
    const uint32_t invalid_periods[] = {0U,     1U,     8999U,  11001U,    15000U,
                                        17999U, 22001U, 65535U, UINT32_MAX};
    for (size_t i = 0U; i < sizeof(invalid_periods) / sizeof(invalid_periods[0]); ++i) {
        uint16_t raw = 123U;
        assert(!mt6701_pwm_decode(invalid_periods[i], 10000U, &raw));
        assert(raw == 123U);
    }
    const uint32_t invalid_highs[] = {0U, 1U, 74U, 20560U, 20594U, 20595U, UINT32_MAX};
    for (size_t i = 0U; i < sizeof(invalid_highs) / sizeof(invalid_highs[0]); ++i) {
        uint16_t raw = 123U;
        assert(!mt6701_pwm_decode(20595U, invalid_highs[i], &raw));
        assert(raw == 123U);
    }
    assert(!mt6701_pwm_decode(20595U, 10320U, NULL));
}

int main(void)
{
    test_fixed_angles();
    test_all_positions_at_both_rates();
    test_invalid_frames_leave_output_unchanged();
    return 0;
}
