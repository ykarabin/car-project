#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "remote_protocol.h"

static unsigned feed(remote_parser_t *parser, const char *line, remote_command_t *output)
{
    unsigned commands = 0U;
    for (size_t i = 0U; line[i] != '\0'; ++i) {
        if (remote_parser_push(parser, (uint8_t)line[i], output) == REMOTE_PARSE_COMMAND) {
            ++commands;
        }
    }
    return commands;
}

static void same_command(const remote_command_t *a, const remote_command_t *b)
{
    assert(a->steering == b->steering && a->throttle == b->throttle);
    assert(a->drive_mode == b->drive_mode && a->status == b->status);
    assert(a->sequence == b->sequence);
}

static void test_fixed_vectors(void)
{
    /* Check value from the standard CRC-16/CCITT-FALSE test string. */
    assert(remote_crc16("123456789", 9U) == UINT16_C(0x29B1));
    static const struct {
        remote_command_t command;
        const char *line;
    } vectors[] = {{{-321, 654, 2U, REMOTE_STATUS_OK, 42U}, "RC,-321,654,2,0,42*C6DA\n"},
                   {{0, 0, 1U, REMOTE_STATUS_ERROR, 0U}, "RC,0,0,1,1,0*2A6A\n"},
                   {{-1000, 1000, 3U, REMOTE_STATUS_OK, 255U}, "RC,-1000,1000,3,0,255*D0CD\n"}};
    for (size_t i = 0U; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        char line[REMOTE_LINE_SIZE];
        const int length = remote_command_format(line, sizeof(line), &vectors[i].command);
        assert(length == (int)strlen(vectors[i].line));
        assert(strcmp(line, vectors[i].line) == 0);
        remote_parser_t parser = {0};
        remote_command_t decoded;
        assert(feed(&parser, vectors[i].line, &decoded) == 1U);
        same_command(&decoded, &vectors[i].command);
    }
}

static void test_ranges_and_buffers(void)
{
    for (int value = -1000; value <= 1000; ++value) {
        const remote_command_t input = {(int16_t)value, (int16_t)-value, 2U, REMOTE_STATUS_OK,
                                        (uint8_t)(value + 1000)};
        char line[REMOTE_LINE_SIZE];
        assert(remote_command_format(line, sizeof(line), &input) > 0);
        remote_parser_t parser = {0};
        remote_command_t output;
        assert(feed(&parser, line, &output) == 1U);
        same_command(&input, &output);
    }
    const remote_command_t input = {0, 0, 1U, REMOTE_STATUS_ERROR, 255U};
    char expected[REMOTE_LINE_SIZE];
    const int length = remote_command_format(expected, sizeof(expected), &input);
    assert(length > 0);
    for (size_t size = 0U; size <= (size_t)length; ++size) {
        char buffer[REMOTE_LINE_SIZE];
        (void)memset(buffer, 0x55, sizeof(buffer));
        assert(remote_command_format(buffer, size, &input) == -1);
        for (size_t i = size; i < sizeof(buffer); ++i) {
            assert(buffer[i] == 0x55);
        }
    }
    assert(remote_command_format(NULL, 0U, &input) == -1);
    assert(remote_command_format(expected, sizeof(expected), NULL) == -1);
    const remote_command_t invalid[] = {
        {1001, 0, 1U, REMOTE_STATUS_OK, 0U}, {-1001, 0, 1U, REMOTE_STATUS_OK, 0U},
        {0, 1001, 1U, REMOTE_STATUS_OK, 0U}, {0, -1001, 1U, REMOTE_STATUS_OK, 0U},
        {0, 0, 0U, REMOTE_STATUS_OK, 0U},    {0, 0, 4U, REMOTE_STATUS_OK, 0U},
        {0, 0, 1U, (remote_status_t)2, 0U},  {1, 0, 1U, REMOTE_STATUS_ERROR, 0U},
        {0, -1, 1U, REMOTE_STATUS_ERROR, 0U}};
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(remote_command_format(expected, sizeof(expected), &invalid[i]) == -1);
    }
}

static void test_bad_lines(void)
{
    /* CRC is intentionally valid, so these cases exercise syntax/range checks. */
    const char *bodies[] = {"RC,1001,0,1,0,1",
                            "RC,-1001,0,1,0,1",
                            "RC,0,-1001,1,0,1",
                            "RC,0,1001,1,0,1",
                            "RC,0,0,0,0,1",
                            "RC,0,0,4,0,1",
                            "RC,0,0,1,2,1",
                            "RC,1,0,1,1,1",
                            "RC,0,-1,1,1,1",
                            "RC,0,0,1,0,256",
                            "RC,0,0,1,0,-1",
                            "RC,0,0,1,0,-0",
                            "RC,0,0,1,0,1,7",
                            "RC,0,0,1,0,",
                            "RC,,0,1,0,1",
                            "RC,+1,0,1,0,1",
                            "RC, 1,0,1,0,1",
                            "RC,1.5,0,1,0,1",
                            "RC,999999999999999999999999999,0,1,0,1",
                            "XX,0,0,1,0,1"};
    for (size_t i = 0U; i < sizeof(bodies) / sizeof(bodies[0]); ++i) {
        char line[128];
        (void)snprintf(line, sizeof(line), "%s*%04X\n", bodies[i],
                       (unsigned)remote_crc16(bodies[i], strlen(bodies[i])));
        remote_parser_t parser = {0};
        remote_command_t decoded = {123, -456, 2U, REMOTE_STATUS_OK, 42U};
        const remote_command_t unchanged = decoded;
        assert(feed(&parser, line, &decoded) == 0U);
        same_command(&decoded, &unchanged);
    }
    const char *invalid_crc[] = {"RC,0,0,1,0,1\n",       "RC,0,0,1,0,1*0000\n",
                                 "RC,0,0,1,0,1*ZZZZ\n",  "RC,0,0,1,0,1*123\n",
                                 "RC,0,0,1,0,1*12345\n", "RC,0,0,1,0,1*1234junk\n"};
    for (size_t i = 0U; i < sizeof(invalid_crc) / sizeof(invalid_crc[0]); ++i) {
        remote_parser_t parser = {0};
        remote_command_t decoded;
        assert(feed(&parser, invalid_crc[i], &decoded) == 0U);
    }
}

static void test_stream_recovery(void)
{
    const char *valid = "RC,-321,654,2,0,42*C6DA\n";
    remote_parser_t parser = {0};
    remote_command_t command;
    assert(feed(&parser, "\n\r\n", &command) == 0U);
    assert(feed(&parser, "RC,-321,654,2,0,42*C6DA", &command) == 0U);
    assert(remote_parser_push(&parser, '\n', &command) == REMOTE_PARSE_COMMAND);
    assert(feed(&parser, "RC,-321,654,2,0,42*c6da\r\n", &command) == 1U);
    assert(feed(&parser, "garbage\n", &command) == 0U);
    assert(feed(&parser, valid, &command) == 1U);

    for (size_t i = 0U; i < 200U; ++i) {
        assert(remote_parser_push(&parser, 'A', &command) != REMOTE_PARSE_COMMAND);
    }
    assert(remote_parser_push(&parser, '\n', &command) == REMOTE_PARSE_NONE);
    assert(feed(&parser, valid, &command) == 1U);
    assert(feed(&parser, "RC,12", &command) == 0U);
    assert(remote_parser_push(&parser, 0U, &command) == REMOTE_PARSE_INVALID);
    assert(feed(&parser, valid, &command) == 0U); /* Discard through the next LF. */
    assert(feed(&parser, valid, &command) == 1U);

    assert(feed(&parser, "RC,12", &command) == 0U);
    remote_parser_reset(&parser); /* UART error resets a partial line. */
    assert(feed(&parser, valid, &command) == 1U);
    assert(feed(&parser, "RC,-321,654,2,0,42*C6DA", &command) == 0U);
    assert(feed(&parser, valid, &command) == 0U); /* A lost LF merges two lines. */
    assert(feed(&parser, valid, &command) == 1U);

    /* Every single-bit change in the protected text must be rejected. */
    const size_t protected_length = (size_t)(strchr(valid, '*') - valid);
    for (size_t i = 0U; i < protected_length; ++i) {
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            char damaged[REMOTE_LINE_SIZE];
            (void)strcpy(damaged, valid);
            damaged[i] = (char)((uint8_t)damaged[i] ^ (uint8_t)(1U << bit));
            remote_parser_reset(&parser);
            unsigned decoded = 0U;
            for (size_t j = 0U; j < strlen(valid); ++j) {
                decoded += (remote_parser_push(&parser, (uint8_t)damaged[j], &command) ==
                            REMOTE_PARSE_COMMAND) ?
                               1U :
                               0U;
            }
            assert(decoded == 0U);
        }
    }
}

int main(void)
{
    test_fixed_vectors();
    test_ranges_and_buffers();
    test_bad_lines();
    test_stream_recovery();
    return 0;
}
