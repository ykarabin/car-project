#include "remote_protocol.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

bool remote_command_valid(const remote_command_t *command)
{
    return (command != NULL) &&
           (command->steering >= -1000) && (command->steering <= 1000) &&
           (command->throttle >= -1000) && (command->throttle <= 1000) &&
           (command->drive_mode >= 1U) && (command->drive_mode <= 3U) &&
           ((command->status == REMOTE_STATUS_OK) || (command->status == REMOTE_STATUS_ERROR)) &&
           ((command->status != REMOTE_STATUS_ERROR) ||
            ((command->steering == 0) && (command->throttle == 0)));
}

uint16_t remote_crc16(const char *data, size_t length)
{
    /* CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF. */
    uint16_t crc = UINT16_C(0xFFFF);
    for (size_t i = 0U; i < length; ++i) {
        crc ^= (uint16_t)((uint16_t)(uint8_t)data[i] << 8U);
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const bool high_bit = (crc & UINT16_C(0x8000)) != 0U;
            crc = (uint16_t)(crc << 1U);
            if (high_bit) {
                crc ^= UINT16_C(0x1021);
            }
        }
    }
    return crc;
}

int remote_command_format(char *line, size_t size, const remote_command_t *command)
{
    if ((line == NULL) || !remote_command_valid(command)) {
        return -1;
    }
    const int length = snprintf(line, size, "RC,%" PRId32 ",%" PRId32 ",%" PRIu32
                                ",%" PRIu32 ",%" PRIu32,
                                (int32_t)command->steering, (int32_t)command->throttle,
                                (uint32_t)command->drive_mode, (uint32_t)command->status,
                                (uint32_t)command->sequence);
    if ((length < 0) || ((size_t)length >= size)) {
        return -1;
    }
    const uint16_t crc = remote_crc16(line, (size_t)length);
    const int tail = snprintf(line + length, size - (size_t)length,
                              "*%04" PRIX32 "\n", (uint32_t)crc);
    if ((tail != 6) || ((size_t)tail >= size - (size_t)length)) {
        return -1;
    }
    return length + tail;
}

static bool read_number(const char **cursor, char separator,
                         int32_t minimum, int32_t maximum, int32_t *output)
{
    const char *p = *cursor;
    const bool negative = (*p == '-');
    if (negative) {
        if (minimum >= 0) {
            return false;
        }
        ++p;
    }
    if ((*p < '0') || (*p > '9')) {
        return false;
    }
    int32_t value = 0;
    while ((*p >= '0') && (*p <= '9')) {
        value = value * 10 + (*p - '0');
        /* Reject oversized fields before arithmetic can overflow. */
        if (value > 1000) {
            return false;
        }
        ++p;
    }
    value = negative ? -value : value;
    if ((*p != separator) || (value < minimum) || (value > maximum)) {
        return false;
    }
    *output = value;
    *cursor = p + 1;
    return true;
}

static int32_t hex_digit(char byte)
{
    if ((byte >= '0') && (byte <= '9')) {
        return byte - '0';
    }
    if ((byte >= 'A') && (byte <= 'F')) {
        return byte - 'A' + 10;
    }
    if ((byte >= 'a') && (byte <= 'f')) {
        return byte - 'a' + 10;
    }
    return -1;
}

static bool parse_line(const char *line, remote_command_t *command)
{
    const char *star = strchr(line, '*');
    if ((star == NULL) || (strlen(star + 1) != 4U) || (strncmp(line, "RC,", 3U) != 0)) {
        return false;
    }
    uint16_t expected_crc = 0U;
    for (size_t i = 1U; i <= 4U; ++i) {
        const int32_t digit = hex_digit(star[i]);
        if (digit < 0) {
            return false;
        }
        expected_crc = (uint16_t)(((uint32_t)expected_crc << 4U) | (uint32_t)digit);
    }
    if (remote_crc16(line, (size_t)(star - line)) != expected_crc) {
        return false;
    }
    const char *p = line + 3;
    int32_t steering;
    int32_t throttle;
    int32_t mode;
    int32_t status;
    int32_t sequence;
    if (!read_number(&p, ',', -1000, 1000, &steering) ||
        !read_number(&p, ',', -1000, 1000, &throttle) ||
        !read_number(&p, ',', 1, 3, &mode) ||
        !read_number(&p, ',', 0, 1, &status) ||
        !read_number(&p, '*', 0, 255, &sequence)) {
        return false;
    }
    const remote_command_t decoded = {
        .steering = (int16_t)steering, .throttle = (int16_t)throttle,
        .drive_mode = (uint8_t)mode, .status = (remote_status_t)status,
        .sequence = (uint8_t)sequence
    };
    if (!remote_command_valid(&decoded)) {
        return false;
    }
    *command = decoded;
    return true;
}

void remote_parser_reset(remote_parser_t *parser)
{
    parser->used = 0U;
    parser->discarding = false;
}

remote_parse_result_t remote_parser_push(remote_parser_t *parser, uint8_t byte,
                                         remote_command_t *command)
{
    if (byte == '\n') {
        if (parser->discarding || (parser->used == 0U)) {
            remote_parser_reset(parser);
            return REMOTE_PARSE_NONE;
        }
        /* Accept both LF and CRLF line endings. */
        if (parser->line[parser->used - 1U] == '\r') {
            --parser->used;
        }
        parser->line[parser->used] = '\0';
        const bool valid = parse_line(parser->line, command);
        remote_parser_reset(parser);
        return valid ? REMOTE_PARSE_COMMAND : REMOTE_PARSE_INVALID;
    }
    if (parser->discarding) {
        return REMOTE_PARSE_NONE;
    }
    if (((byte < 32U) && (byte != '\r')) || (byte > 126U) ||
        (parser->used >= sizeof(parser->line) - 1U)) {
        parser->discarding = true;
        return REMOTE_PARSE_INVALID;
    }
    parser->line[parser->used++] = (char)byte;
    return REMOTE_PARSE_NONE;
}
