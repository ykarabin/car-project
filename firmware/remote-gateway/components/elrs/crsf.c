#include "crsf.h"

#include <stdbool.h>
#include <string.h>

#define CRSF_SYNC_BYTE UINT8_C(0xC8)
#define CRSF_BROADCAST_ADDRESS UINT8_C(0x00)
#define CRSF_TYPE_CHANNELS UINT8_C(0x16)
#define CRSF_CRC_POLYNOMIAL UINT8_C(0xD5)
#define CRSF_CHANNEL_EXT_MAX UINT16_C(1984)
#define CRSF_CHANNEL_MIN INT32_C(172)
#define CRSF_CHANNEL_MID INT32_C(992)
#define CRSF_CHANNEL_MAX INT32_C(1811)
#define CRSF_CHANNEL_PAYLOAD_SIZE 22U

static uint8_t crc8(const uint8_t *data, size_t size)
{
    uint8_t crc = 0U;

    for (size_t index = 0U; index < size; ++index) {
        crc ^= data[index];

        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const bool high_bit = (crc & UINT8_C(0x80)) != 0U;
            crc = (uint8_t)((uint32_t)crc << 1U);
            if (high_bit) {
                crc ^= CRSF_CRC_POLYNOMIAL;
            }
        }
    }

    return crc;
}

static void discard_bytes(crsf_parser_t *parser, size_t size)
{
    parser->used -= size;
    (void)memmove(parser->buffer, &parser->buffer[size], parser->used);
}

static bool decode_channels(const uint8_t *payload, uint16_t required_channel_mask,
                            uint16_t channels[CRSF_CHANNEL_COUNT])
{
    uint32_t bits = 0U;
    uint32_t bit_count = 0U;
    size_t payload_index = 0U;
    bool valid = true;

    /* Explicit LSB-first unpacking avoids compiler-dependent packed bitfields. */
    for (size_t channel = 0U; channel < CRSF_CHANNEL_COUNT; ++channel) {
        while (bit_count < 11U) {
            bits |= (uint32_t)payload[payload_index] << bit_count;
            ++payload_index;
            bit_count += 8U;
        }

        channels[channel] = (uint16_t)(bits & UINT32_C(0x07FF));
        bits >>= 11U;
        bit_count -= 11U;
        if ((((uint32_t)required_channel_mask & (UINT32_C(1) << channel)) != 0U) &&
            (channels[channel] > CRSF_CHANNEL_EXT_MAX)) {
            valid = false;
        }
    }

    return valid;
}

void crsf_parser_reset(crsf_parser_t *parser)
{
    parser->used = 0U;
}

crsf_result_t crsf_parser_push(crsf_parser_t *parser, uint8_t byte,
                               uint16_t channels[CRSF_CHANNEL_COUNT])
{
    /* A complete maximum-size frame is always consumed below. */
    parser->buffer[parser->used] = byte;
    ++parser->used;

    while (parser->used > 0U) {
        if ((parser->buffer[0] != CRSF_SYNC_BYTE) &&
            (parser->buffer[0] != CRSF_BROADCAST_ADDRESS)) {
            discard_bytes(parser, 1U);
            continue;
        }

        if (parser->used < 2U) {
            break;
        }

        const size_t length = parser->buffer[1];
        if ((length < 2U) || (length > (CRSF_MAX_FRAME_SIZE - 2U))) {
            ++parser->frame_errors;
            discard_bytes(parser, 1U);
            continue;
        }

        const size_t frame_size = length + 2U;
        if (parser->used < frame_size) {
            break;
        }

        if (crc8(&parser->buffer[2], length - 1U) !=
            parser->buffer[frame_size - 1U]) {
            ++parser->crc_errors;
            /* Scan retained bytes for a frame after a corrupted length/CRC. */
            discard_bytes(parser, 1U);
            continue;
        }

        crsf_result_t result = CRSF_RESULT_NONE;
        if (parser->buffer[2] == CRSF_TYPE_CHANNELS) {
            if (length < (CRSF_CHANNEL_PAYLOAD_SIZE + 2U)) {
                ++parser->frame_errors;
            } else if (decode_channels(&parser->buffer[3], parser->required_channel_mask, channels)) {
                ++parser->rc_frames;
                result = CRSF_RESULT_CHANNELS;
            } else {
                ++parser->range_errors;
                result = CRSF_RESULT_INVALID_CHANNELS;
            }
        }

        /* Future trailing payload fields are covered by CRC and ignored. */
        discard_bytes(parser, frame_size);
        if (result != CRSF_RESULT_NONE) {
            return result;
        }
    }

    return CRSF_RESULT_NONE;
}

int16_t crsf_normalize(uint16_t raw)
{
    int32_t normalized;
    const int32_t value = (int32_t)raw;

    if (value <= CRSF_CHANNEL_MIN) {
        normalized = -1000;
    } else if (value >= CRSF_CHANNEL_MAX) {
        normalized = 1000;
    } else if (value < CRSF_CHANNEL_MID) {
        normalized = ((value - CRSF_CHANNEL_MID) * 1000) /
                     (CRSF_CHANNEL_MID - CRSF_CHANNEL_MIN);
    } else {
        normalized = ((value - CRSF_CHANNEL_MID) * 1000) /
                     (CRSF_CHANNEL_MAX - CRSF_CHANNEL_MID);
    }

    return (int16_t)normalized;
}
