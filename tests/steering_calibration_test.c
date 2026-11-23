#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "steering_calibration.h"

static void expect_position(const steering_calibration_t *calibration, uint16_t raw, int16_t expected)
{
    int16_t position = 123;
    assert(steering_calibration_position(calibration, raw, &position));
    assert(position == expected);
}

static void test_geometry_and_working_margin(void)
{
    const steering_calibration_t normal = {1000U, 500U, 1600U};
    assert(steering_calibration_valid(&normal));
    expect_position(&normal, 1000U, 0);
    expect_position(&normal, 500U, -1000);
    expect_position(&normal, 515U, -1000); /* 3% of left half-travel is 15 counts. */
    expect_position(&normal, 903U, -200);
    expect_position(&normal, 1291U, 500); /* Different right half-travel: 582 working counts. */
    expect_position(&normal, 1582U, 1000);
    expect_position(&normal, 1600U, 1000);

    const steering_calibration_t reversed = {1000U, 1600U, 500U};
    assert(steering_calibration_valid(&reversed));
    expect_position(&reversed, 1000U, 0);
    expect_position(&reversed, 1600U, -1000);
    expect_position(&reversed, 500U, 1000);

    const steering_calibration_t wrapping = {4090U, 3700U, 500U};
    assert(steering_calibration_valid(&wrapping));
    assert(steering_calibration_delta(4090U, 0U) == 6);
    assert(steering_calibration_delta(0U, 4090U) == -6);
    int16_t before;
    int16_t after;
    assert(steering_calibration_position(&wrapping, 4095U, &before));
    assert(steering_calibration_position(&wrapping, 0U, &after));
    assert(before > 0 && after >= before && (after - before) < 4);
    expect_position(&wrapping, 3700U, -1000);
    expect_position(&wrapping, 500U, 1000);
}

static void test_invalid_geometry(void)
{
    const steering_calibration_t bad[] = {
        {1000U, 1000U, 1600U}, {1000U, 999U, 1600U},
        {1000U, 500U, 500U}, {1000U, 1100U, 1600U},
        {4096U, 500U, 1600U}, {1000U, 4096U, 1600U},
        {1000U, 500U, 4096U}, {1000U, 0U, 2048U},
        {0U, 2048U, 500U}
    };
    for (size_t i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        int16_t position = 321;
        assert(!steering_calibration_valid(&bad[i]));
        assert(!steering_calibration_position(&bad[i], 1000U, &position));
        assert(position == 321);
    }
    assert(!steering_calibration_valid(NULL));
    const steering_calibration_t good = {1000U, 500U, 1600U};
    int16_t position;
    assert(!steering_calibration_position(&good, 4096U, &position));
    assert(!steering_calibration_position(&good, 1000U, NULL));
}

static void test_records_and_interrupted_writes(void)
{
    const steering_calibration_t calibration = {1000U, 500U, 1600U};
    uint32_t words[STEERING_CAL_RECORD_WORDS];
    assert(steering_calibration_encode(&calibration, 7U, words));
    /* Independent fixture: Python zlib.crc32(struct.pack('<5I', ...)). */
    assert(words[5] == UINT32_C(0xADA06A68));
    steering_calibration_t decoded = {0};
    uint32_t sequence = 0U;
    assert(steering_calibration_decode(words, &decoded, &sequence));
    assert(sequence == 7U && decoded.center == 1000U && decoded.left == 500U && decoded.right == 1600U);
    for (size_t word = 0U; word < STEERING_CAL_RECORD_WORDS; ++word) {
        for (unsigned bit = 0U; bit < 32U; ++bit) {
            uint32_t corrupted[STEERING_CAL_RECORD_WORDS];
            (void)memcpy(corrupted, words, sizeof(words));
            corrupted[word] ^= UINT32_C(1) << bit;
            assert(!steering_calibration_decode(corrupted, &decoded, &sequence));
        }
    }
    for (size_t written = 0U; written < STEERING_CAL_RECORD_WORDS; ++written) {
        uint32_t partial[STEERING_CAL_RECORD_WORDS];
        for (size_t i = 0U; i < STEERING_CAL_RECORD_WORDS; ++i) {
            partial[i] = (i < written) ? words[i] : UINT32_MAX;
        }
        assert(!steering_calibration_decode(partial, &decoded, &sequence));
    }
    assert(!steering_calibration_encode(NULL, 0U, words));
    assert(!steering_calibration_decode(NULL, &decoded, &sequence));
    assert(steering_calibration_sequence_newer(0U, UINT32_MAX));
    assert(steering_calibration_sequence_newer(8U, 7U));
    assert(!steering_calibration_sequence_newer(7U, 7U));
    assert(!steering_calibration_sequence_newer(UINT32_MAX, 0U));
}

int main(void)
{
    test_geometry_and_working_margin();
    test_invalid_geometry();
    test_records_and_interrupted_writes();
    return 0;
}
