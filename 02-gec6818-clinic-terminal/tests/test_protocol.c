#include "clinic_frame.h"
#include "clinic_protocol.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                   \
    do                                                                      \
    {                                                                       \
        if (!(condition))                                                   \
        {                                                                   \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

static void test_ping_parse(void)
{
    const char *line = "{\"type\":\"ping\",\"request_id\":1}";
    const char *with_extra_fields =
        "{\"type\":\"ping\",\"request_id\":6,"
        "\"meta\":{\"ready\":true,\"items\":[1,null,\"ok\"]}}";
    const char *with_nested_shadow_fields =
        "{\"meta\":{\"type\":\"login\",\"request_id\":99},"
        "\"type\":\"ping\",\"request_id\":7}";
    clinic_request_t request;

    CHECK(clinic_protocol_parse_request(line, strlen(line), &request) == CLINIC_PROTOCOL_OK);
    CHECK(request.type == CLINIC_REQUEST_PING);
    CHECK(request.request_id == 1U);
    CHECK(clinic_protocol_parse_request(
              with_extra_fields,
              strlen(with_extra_fields),
              &request) == CLINIC_PROTOCOL_OK);
    CHECK(request.request_id == 6U);
    CHECK(clinic_protocol_parse_request(
              with_nested_shadow_fields,
              strlen(with_nested_shadow_fields),
              &request) == CLINIC_PROTOCOL_OK);
    CHECK(request.request_id == 7U);
}

static void test_protocol_encoding(void)
{
    char output[256];
    const char *expected;
    int length;

    expected = "{\"type\":\"ping\",\"request_id\":1}\n";
    length = clinic_protocol_encode_ping(1U, output, sizeof(output));
    CHECK(length == (int)strlen(expected));
    CHECK(strcmp(output, expected) == 0);
    CHECK(length > 0 && output[length - 1] == '\n');
    CHECK(strstr(output, "\\n") == NULL);

    expected =
        "{\"ok\":true,\"type\":\"pong\",\"request_id\":4294967297,"
        "\"message\":\"clinic server is alive\"}\n";
    length = clinic_protocol_encode_pong(
        UINT64_C(4294967297),
        output,
        sizeof(output));
    CHECK(length == (int)strlen(expected));
    CHECK(strcmp(output, expected) == 0);
    CHECK(length > 0 && output[length - 1] == '\n');
    CHECK(strstr(output, "\\n") == NULL);

    expected =
        "{\"ok\":false,\"request_id\":7,\"error_code\":\"INVALID_JSON\","
        "\"message\":\"invalid request json\"}\n";
    length = clinic_protocol_encode_error(
        7U,
        "INVALID_JSON",
        "invalid request json",
        output,
        sizeof(output));
    CHECK(length == (int)strlen(expected));
    CHECK(strcmp(output, expected) == 0);
    CHECK(length > 0 && output[length - 1] == '\n');
    CHECK(strstr(output, "\\n") == NULL);
}

static void test_encoding_buffer_too_small(void)
{
    char output[8];

    memset(output, 'X', sizeof(output));
    CHECK(clinic_protocol_encode_ping(1U, output, 4U) == -1);
    CHECK(output[3] == '\0');
    CHECK(output[4] == 'X');

    memset(output, 'X', sizeof(output));
    CHECK(clinic_protocol_encode_pong(1U, output, 4U) == -1);
    CHECK(output[3] == '\0');
    CHECK(output[4] == 'X');

    memset(output, 'X', sizeof(output));
    CHECK(clinic_protocol_encode_error(
              1U,
              "INVALID_JSON",
              "invalid request json",
              output,
              4U) == -1);
    CHECK(output[3] == '\0');
    CHECK(output[4] == 'X');

    output[0] = 'X';
    CHECK(clinic_protocol_encode_ping(1U, output, 0U) == -1);
    CHECK(output[0] == 'X');
    CHECK(clinic_protocol_encode_ping(1U, NULL, 0U) == -1);
    CHECK(clinic_protocol_encode_error(
              1U,
              "INVALID\"JSON",
              "invalid request json",
              output,
              sizeof(output)) == -1);
    CHECK(clinic_protocol_encode_error(
              1U,
              "INVALID_JSON",
              "invalid\nrequest json",
              output,
              sizeof(output)) == -1);
}

static void test_split_frame(void)
{
    clinic_frame_buffer_t buffer;
    clinic_request_t request;
    char line[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t line_length = 0U;
    const char *part1 = "{\"type\":\"ping\",";
    const char *part2 = "\"request_id\":2}\n";

    clinic_frame_buffer_init(&buffer);
    CHECK(clinic_frame_buffer_append(
              &buffer,
              part1,
              strlen(part1)) == 0);
    CHECK(buffer.length == strlen(part1));
    CHECK(clinic_frame_buffer_next(&buffer, line, sizeof(line), &line_length) == CLINIC_FRAME_NEED_MORE);
    CHECK(clinic_frame_buffer_append(
              &buffer,
              part2,
              strlen(part2)) == 0);
    CHECK(clinic_frame_buffer_next(&buffer, line, sizeof(line), &line_length) == CLINIC_FRAME_READY);
    CHECK(strcmp(line, "{\"type\":\"ping\",\"request_id\":2}") == 0);
    CHECK(clinic_protocol_parse_request(line, line_length, &request) == CLINIC_PROTOCOL_OK);
    CHECK(request.request_id == 2U);
}

static void test_sticky_frames(void)
{
    clinic_frame_buffer_t buffer;
    clinic_request_t request;
    char line[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t line_length = 0U;
    const char *frames =
        "{\"type\":\"ping\",\"request_id\":3}\n"
        "{\"type\":\"ping\",\"request_id\":4}\n";

    clinic_frame_buffer_init(&buffer);
    CHECK(clinic_frame_buffer_append(&buffer, frames, strlen(frames)) == 0);
    CHECK(clinic_frame_buffer_next(&buffer, line, sizeof(line), &line_length) == CLINIC_FRAME_READY);
    CHECK(clinic_protocol_parse_request(line, line_length, &request) == CLINIC_PROTOCOL_OK);
    CHECK(request.request_id == 3U);
    CHECK(clinic_frame_buffer_next(&buffer, line, sizeof(line), &line_length) == CLINIC_FRAME_READY);
    CHECK(clinic_protocol_parse_request(line, line_length, &request) == CLINIC_PROTOCOL_OK);
    CHECK(request.request_id == 4U);
    CHECK(clinic_frame_buffer_next(&buffer, line, sizeof(line), &line_length) == CLINIC_FRAME_NEED_MORE);
}

static void test_invalid_json(void)
{
    clinic_request_t request;
    const char *invalid = "{not-json}";
    const char *missing_comma =
        "{\"type\":\"ping\" \"request_id\":1}";
    const char *trailing_comma =
        "{\"type\":\"ping\",\"request_id\":1,}";
    const char *invalid_nested_value =
        "{\"type\":\"ping\",\"request_id\":1,\"meta\":[true,]}";
    const char *nested_fields_only =
        "{\"meta\":{\"type\":\"ping\",\"request_id\":1}}";
    const char *duplicate_request_id =
        "{\"type\":\"ping\",\"request_id\":1,\"request_id\":2}";
    const char embedded_null[] =
        "{\"type\":\"ping\",\"request_id\":1}\0garbage}";
    const char *unknown = "{\"type\":\"login\",\"request_id\":5}";

    CHECK(clinic_protocol_parse_request(invalid, strlen(invalid), &request) == CLINIC_PROTOCOL_INVALID_JSON);
    CHECK(clinic_protocol_parse_request(
              missing_comma,
              strlen(missing_comma),
              &request) == CLINIC_PROTOCOL_INVALID_JSON);
    CHECK(clinic_protocol_parse_request(
              trailing_comma,
              strlen(trailing_comma),
              &request) == CLINIC_PROTOCOL_INVALID_JSON);
    CHECK(clinic_protocol_parse_request(
              invalid_nested_value,
              strlen(invalid_nested_value),
              &request) == CLINIC_PROTOCOL_INVALID_JSON);
    CHECK(clinic_protocol_parse_request(
              nested_fields_only,
              strlen(nested_fields_only),
              &request) == CLINIC_PROTOCOL_INVALID_JSON);
    CHECK(clinic_protocol_parse_request(
              duplicate_request_id,
              strlen(duplicate_request_id),
              &request) == CLINIC_PROTOCOL_INVALID_JSON);
    CHECK(clinic_protocol_parse_request(
              embedded_null,
              sizeof(embedded_null) - 1U,
              &request) == CLINIC_PROTOCOL_INVALID_JSON);
    CHECK(clinic_protocol_parse_request(unknown, strlen(unknown), &request) == CLINIC_PROTOCOL_UNKNOWN_REQUEST);
}

static void test_empty_frame(void)
{
    clinic_frame_buffer_t buffer;
    clinic_request_t request;
    char line[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t line_length = 1U;

    clinic_frame_buffer_init(&buffer);
    CHECK(clinic_frame_buffer_append(&buffer, "\n", strlen("\n")) == 0);
    CHECK(clinic_frame_buffer_next(
              &buffer,
              line,
              sizeof(line),
              &line_length) == CLINIC_FRAME_READY);
    CHECK(line_length == 0U);
    CHECK(line[0] == '\0');
    CHECK(clinic_protocol_parse_request(
              line,
              line_length,
              &request) == CLINIC_PROTOCOL_INVALID_JSON);
}

static void test_frame_size_limits(void)
{
    clinic_frame_buffer_t buffer;
    char line[CLINIC_MAX_FRAME_SIZE + 1U];
    char maximum[CLINIC_MAX_FRAME_SIZE + 1U];
    char oversized[CLINIC_MAX_FRAME_SIZE + 2U];
    size_t line_length = 0U;

    memset(maximum, 'x', sizeof(maximum));
    maximum[sizeof(maximum) - 1U] = '\n';
    clinic_frame_buffer_init(&buffer);
    CHECK(clinic_frame_buffer_append(&buffer, maximum, sizeof(maximum)) == 0);
    CHECK(clinic_frame_buffer_next(
              &buffer,
              line,
              sizeof(line),
              &line_length) == CLINIC_FRAME_READY);
    CHECK(line_length == CLINIC_MAX_FRAME_SIZE);
    CHECK(line[CLINIC_MAX_FRAME_SIZE] == '\0');

    memset(oversized, 'x', sizeof(oversized));
    oversized[sizeof(oversized) - 1U] = '\n';
    clinic_frame_buffer_init(&buffer);
    CHECK(clinic_frame_buffer_append(&buffer, oversized, sizeof(oversized)) == 0);
    CHECK(clinic_frame_buffer_next(&buffer, line, sizeof(line), &line_length) == CLINIC_FRAME_TOO_LARGE);
}

static void test_parse_size_limit(void)
{
    clinic_request_t request;
    char maximum[CLINIC_MAX_FRAME_SIZE];
    char oversized[CLINIC_MAX_FRAME_SIZE + 1U];
    const char *request_json = "{\"type\":\"ping\",\"request_id\":8}";
    size_t request_length = strlen(request_json);

    memcpy(maximum, request_json, request_length);
    memset(
        maximum + request_length,
        ' ',
        sizeof(maximum) - request_length);
    CHECK(clinic_protocol_parse_request(
              maximum,
              sizeof(maximum),
              &request) == CLINIC_PROTOCOL_OK);
    CHECK(request.request_id == 8U);

    memcpy(oversized, maximum, sizeof(maximum));
    oversized[sizeof(oversized) - 1U] = ' ';
    CHECK(clinic_protocol_parse_request(
              oversized,
              sizeof(oversized),
              &request) == CLINIC_PROTOCOL_MESSAGE_TOO_LARGE);
}

static void test_receive_buffer_limit(void)
{
    clinic_frame_buffer_t buffer;
    char data[CLINIC_RECV_BUFFER_SIZE];
    char extra = 'x';

    memset(data, 'x', sizeof(data));
    clinic_frame_buffer_init(&buffer);
    CHECK(clinic_frame_buffer_append(&buffer, data, sizeof(data)) == 0);
    CHECK(buffer.length == CLINIC_RECV_BUFFER_SIZE);
    CHECK(clinic_frame_buffer_append(
              &buffer,
              &extra,
              sizeof(extra)) == CLINIC_FRAME_TOO_LARGE);
    CHECK(buffer.length == CLINIC_RECV_BUFFER_SIZE);
}

int main(void)
{
    test_ping_parse();
    test_protocol_encoding();
    test_encoding_buffer_too_small();
    test_split_frame();
    test_sticky_frames();
    test_invalid_json();
    test_empty_frame();
    test_frame_size_limits();
    test_parse_size_limit();
    test_receive_buffer_limit();

    if (failures != 0)
    {
        fprintf(stderr, "%d protocol test(s) failed\n", failures);
        return 1;
    }

    puts("protocol tests passed");
    return 0;
}
