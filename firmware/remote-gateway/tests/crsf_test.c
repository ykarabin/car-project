/* Keep checks enabled in Release host builds as well. */
#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "crsf.h"

/* Values observed on the ES900. CH13 is outside the control-channel range. */
static const uint16_t RECEIVER_CHANNELS[CRSF_CHANNEL_COUNT] = {
    996U, 989U, 174U, 994U,  191U,  191U, 191U,  191U,
    191U, 191U, 978U, 1792U, 2047U, 191U, 1811U, 1811U};

static void make_frame(const uint16_t channels[CRSF_CHANNEL_COUNT], uint8_t frame[26])
{
    (void)memset(frame, 0, 26U);
    frame[0] = UINT8_C(0xC8);
    frame[1] = 24U;
    frame[2] = UINT8_C(0x16);

    /* Pack each bit independently of the parser's shift-register unpacking. */
    for (size_t channel = 0U; channel < CRSF_CHANNEL_COUNT; ++channel) {
        for (size_t bit = 0U; bit < 11U; ++bit) {
            const size_t offset = (channel * 11U) + bit;
            const uint32_t value = ((uint32_t)channels[channel] >> bit) & UINT32_C(1);
            frame[3U + (offset / 8U)] |= (uint8_t)(value << (offset % 8U));
        }
    }

    uint32_t crc = 0U;
    for (size_t index = 2U; index < 25U; ++index) {
        crc ^= frame[index];
        for (size_t bit = 0U; bit < 8U; ++bit) {
            crc <<= 1U;
            if ((crc & UINT32_C(0x100)) != 0U) {
                crc ^= UINT32_C(0x1D5);
            }
        }
    }
    frame[25] = (uint8_t)crc;
}

static crsf_result_t parse_frame(crsf_parser_t *parser, const uint8_t frame[26],
                                 uint16_t channels[CRSF_CHANNEL_COUNT])
{
    crsf_result_t result = CRSF_RESULT_NONE;
    for (size_t index = 0U; index < 26U; ++index) {
        result = crsf_parser_push(parser, frame[index], channels);
        assert(parser->used < CRSF_MAX_FRAME_SIZE);
        if (index < 25U) {
            assert(result == CRSF_RESULT_NONE);
        }
    }
    return result;
}

int main(void)
{
    uint16_t channels[CRSF_CHANNEL_COUNT];
    uint16_t output[CRSF_CHANNEL_COUNT];
    uint8_t frame[26];
    crsf_parser_t parser = {.required_channel_mask = UINT16_C(0x0007)};

    make_frame(RECEIVER_CHANNELS, frame);
    assert(parse_frame(&parser, frame, output) == CRSF_RESULT_CHANNELS);
    assert(memcmp(output, RECEIVER_CHANNELS, sizeof(output)) == 0);
    assert(parser.rc_frames == 1U && parser.range_errors == 0U);

    /* Selecting CH13 makes the same value fail the application's range policy. */
    parser.required_channel_mask |= UINT16_C(0x1000);
    assert(parse_frame(&parser, frame, output) == CRSF_RESULT_INVALID_CHANNELS);
    assert(parser.rc_frames == 1U && parser.range_errors == 1U);
    parser.required_channel_mask = UINT16_C(0x0007);

    /* Every required channel must still reject values above the supported range. */
    for (size_t channel = 0U; channel < 3U; ++channel) {
        (void)memcpy(channels, RECEIVER_CHANNELS, sizeof(channels));
        channels[channel] = 1985U;
        make_frame(channels, frame);
        assert(parse_frame(&parser, frame, output) == CRSF_RESULT_INVALID_CHANNELS);
    }
    assert(parser.rc_frames == 1U && parser.range_errors == 4U);

    /* Recovery and reset retain the required-channel selection. */
    crsf_parser_reset(&parser);
    assert(parser.required_channel_mask == UINT16_C(0x0007));
    make_frame(RECEIVER_CHANNELS, frame);
    assert(parse_frame(&parser, frame, output) == CRSF_RESULT_CHANNELS);
    frame[25] ^= UINT8_C(1);
    assert(parse_frame(&parser, frame, output) == CRSF_RESULT_NONE);
    assert(parser.rc_frames == 2U && parser.crc_errors > 0U);

    assert(crsf_normalize(172U) == -1000);
    assert(crsf_normalize(992U) == 0);
    assert(crsf_normalize(1811U) == 1000);
    return 0;
}
