#ifndef REMOTE_PROTOCOL_H
#define REMOTE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REMOTE_UART_BAUD_RATE 115200
#define REMOTE_SEND_PERIOD_MS 20U
#define REMOTE_LINE_SIZE 64U

typedef enum {
    REMOTE_STATUS_OK = 0,
    REMOTE_STATUS_ERROR = 1
} remote_status_t;

typedef struct {
    int16_t steering;       /* -1000 left .. 0 center .. 1000 right */
    int16_t throttle;       /* -1000 reverse .. 0 neutral .. 1000 forward */
    uint8_t drive_mode;     /* 1, 2 or 3 */
    remote_status_t status;
    uint8_t sequence;       /* Increment for each transmission; wraps to 0. */
} remote_command_t;

typedef enum {
    REMOTE_PARSE_NONE,
    REMOTE_PARSE_COMMAND,
    REMOTE_PARSE_INVALID
} remote_parse_result_t;

typedef struct {
    char line[REMOTE_LINE_SIZE];
    size_t used;
    bool discarding;
} remote_parser_t;

bool remote_command_valid(const remote_command_t *command);
uint16_t remote_crc16(const char *data, size_t length);

/* Format: RC,steering,throttle,mode,status,sequence*CRC16\n.
 * Returns the number of bytes to send, or -1 for invalid input/small buffer. */
int remote_command_format(char *line, size_t size, const remote_command_t *command);

/* Feed UART bytes one at a time. Output changes only after a valid whole line. */
void remote_parser_reset(remote_parser_t *parser);
remote_parse_result_t remote_parser_push(remote_parser_t *parser, uint8_t byte,
                                         remote_command_t *command);

#endif
