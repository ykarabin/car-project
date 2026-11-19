#ifndef REMOTE_GATEWAY_CRSF_H
#define REMOTE_GATEWAY_CRSF_H

#include <stddef.h>
#include <stdint.h>

#define CRSF_CHANNEL_COUNT 16U
#define CRSF_MAX_FRAME_SIZE 64U

typedef enum {
    CRSF_RESULT_NONE = 0,
    CRSF_RESULT_CHANNELS,
    CRSF_RESULT_INVALID_CHANNELS
} crsf_result_t;

typedef struct {
    uint8_t buffer[CRSF_MAX_FRAME_SIZE];
    size_t used;
    uint16_t required_channel_mask;
    uint32_t rc_frames;
    uint32_t crc_errors;
    uint32_t frame_errors;
    uint32_t range_errors;
} crsf_parser_t;

/* Zero-initialize and set a nonzero required_channel_mask before first use.
 * Bit 0 selects CH1. Reset preserves the mask and diagnostic counters.
 */
void crsf_parser_reset(crsf_parser_t *parser);
crsf_result_t crsf_parser_push(crsf_parser_t *parser, uint8_t byte,
                               uint16_t channels[CRSF_CHANNEL_COUNT]);
int16_t crsf_normalize(uint16_t raw);

#endif
