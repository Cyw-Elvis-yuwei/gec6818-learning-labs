/*
 * 文件作用：基础协议工具，主要处理 ping/pong、通用 error 和 JSON 外层合法性。
 * 它实现有界扫描，检查 JSON 对象、字符串、数字、嵌套深度和 request_id，避免未终止文本、
 * 超长消息或非法 JSON 进入后续处理。
 *
 * 注意：注册、登录、医生和号单等业务字段由 clinic_json.c 处理；本文件不是 Core，也不
 * 访问 Store。两者分工是“基础探活协议”和“医疗业务 JSON”。
 *
 * 阅读地图：前半部分是无动态内存的 JSON 语法扫描器；parse_top_level_request_fields
 * 只提取基础 type/request_id；parse_request 识别 ping；encode_ping/pong/error 负责生成
 * 可直接通过换行分帧发送的基础消息。
 */
#include "clinic_protocol.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define CLINIC_JSON_MAX_DEPTH 32U

static void json_skip_space(const char **cursor, const char *end)
{
    while (*cursor < end &&
           (**cursor == ' ' || **cursor == '\t' ||
            **cursor == '\n' || **cursor == '\r'))
    {
        ++(*cursor);
    }
}

static int json_skip_value(
    const char **cursor,
    const char *end,
    unsigned int depth);

static int json_skip_string(const char **cursor, const char *end)
{
    if (*cursor >= end || **cursor != '"')
    {
        return -1;
    }

    ++(*cursor);
    while (*cursor < end)
    {
        unsigned char character = (unsigned char)**cursor;
        ++(*cursor);

        if (character == '"')
        {
            return 0;
        }

        if (character < 0x20U)
        {
            return -1;
        }

        if (character == '\\')
        {
            unsigned char escape;
            int index;

            if (*cursor >= end)
            {
                return -1;
            }

            escape = (unsigned char)**cursor;
            ++(*cursor);
            if (escape == 'u')
            {
                for (index = 0; index < 4; ++index)
                {
                    if (*cursor >= end ||
                        !isxdigit((unsigned char)**cursor))
                    {
                        return -1;
                    }
                    ++(*cursor);
                }
            }
            else if (strchr("\"\\/bfnrt", escape) == NULL)
            {
                return -1;
            }
        }
    }

    return -1;
}

static int json_skip_number(const char **cursor, const char *end)
{
    if (*cursor < end && **cursor == '-')
    {
        ++(*cursor);
    }

    if (*cursor >= end)
    {
        return -1;
    }

    if (**cursor == '0')
    {
        ++(*cursor);
    }
    else if (**cursor >= '1' && **cursor <= '9')
    {
        do
        {
            ++(*cursor);
        } while (*cursor < end &&
                 isdigit((unsigned char)**cursor));
    }
    else
    {
        return -1;
    }

    if (*cursor < end && **cursor == '.')
    {
        ++(*cursor);
        if (*cursor >= end || !isdigit((unsigned char)**cursor))
        {
            return -1;
        }
        do
        {
            ++(*cursor);
        } while (*cursor < end &&
                 isdigit((unsigned char)**cursor));
    }

    if (*cursor < end && (**cursor == 'e' || **cursor == 'E'))
    {
        ++(*cursor);
        if (*cursor < end && (**cursor == '+' || **cursor == '-'))
        {
            ++(*cursor);
        }
        if (*cursor >= end || !isdigit((unsigned char)**cursor))
        {
            return -1;
        }
        do
        {
            ++(*cursor);
        } while (*cursor < end &&
                 isdigit((unsigned char)**cursor));
    }

    return 0;
}

static int json_skip_literal(
    const char **cursor,
    const char *end,
    const char *literal)
{
    size_t length = strlen(literal);

    if ((size_t)(end - *cursor) < length ||
        memcmp(*cursor, literal, length) != 0)
    {
        return -1;
    }

    *cursor += length;
    return 0;
}

static int json_skip_array(
    const char **cursor,
    const char *end,
    unsigned int depth)
{
    ++(*cursor);
    json_skip_space(cursor, end);
    if (*cursor < end && **cursor == ']')
    {
        ++(*cursor);
        return 0;
    }

    for (;;)
    {
        if (json_skip_value(cursor, end, depth + 1U) != 0)
        {
            return -1;
        }

        json_skip_space(cursor, end);
        if (*cursor < end && **cursor == ']')
        {
            ++(*cursor);
            return 0;
        }
        if (*cursor >= end || **cursor != ',')
        {
            return -1;
        }
        ++(*cursor);
        json_skip_space(cursor, end);
    }
}

static int json_skip_object(
    const char **cursor,
    const char *end,
    unsigned int depth)
{
    ++(*cursor);
    json_skip_space(cursor, end);
    if (*cursor < end && **cursor == '}')
    {
        ++(*cursor);
        return 0;
    }

    for (;;)
    {
        if (json_skip_string(cursor, end) != 0)
        {
            return -1;
        }

        json_skip_space(cursor, end);
        if (*cursor >= end || **cursor != ':')
        {
            return -1;
        }
        ++(*cursor);

        if (json_skip_value(cursor, end, depth + 1U) != 0)
        {
            return -1;
        }

        json_skip_space(cursor, end);
        if (*cursor < end && **cursor == '}')
        {
            ++(*cursor);
            return 0;
        }
        if (*cursor >= end || **cursor != ',')
        {
            return -1;
        }
        ++(*cursor);
        json_skip_space(cursor, end);
    }
}

static int json_skip_value(
    const char **cursor,
    const char *end,
    unsigned int depth)
{
    if (depth > CLINIC_JSON_MAX_DEPTH)
    {
        return -1;
    }

    json_skip_space(cursor, end);
    if (*cursor >= end)
    {
        return -1;
    }

    if (**cursor == '"')
    {
        return json_skip_string(cursor, end);
    }
    if (**cursor == '{')
    {
        return json_skip_object(cursor, end, depth);
    }
    if (**cursor == '[')
    {
        return json_skip_array(cursor, end, depth);
    }
    if (**cursor == 't')
    {
        return json_skip_literal(cursor, end, "true");
    }
    if (**cursor == 'f')
    {
        return json_skip_literal(cursor, end, "false");
    }
    if (**cursor == 'n')
    {
        return json_skip_literal(cursor, end, "null");
    }
    if (**cursor == '-' || isdigit((unsigned char)**cursor))
    {
        return json_skip_number(cursor, end);
    }

    return -1;
}

/* 验证输入恰好是一个完整 JSON 对象，拒绝尾随垃圾和超过最大嵌套深度的数据。 */
static int json_is_valid_object(const char *text, size_t length)
{
    const char *cursor = text;
    const char *end = text + length;

    json_skip_space(&cursor, end);
    if (cursor >= end || *cursor != '{' ||
        json_skip_value(&cursor, end, 0U) != 0)
    {
        return 0;
    }

    json_skip_space(&cursor, end);
    return cursor == end;
}

static int json_string_can_encode_without_escaping(const char *text)
{
    while (*text != '\0')
    {
        unsigned char character = (unsigned char)*text;
        if (character < 0x20U || character == '"' || character == '\\')
        {
            return 0;
        }
        ++text;
    }

    return 1;
}

static const char *skip_whitespace(const char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text))
    {
        ++text;
    }
    return text;
}

static int json_span_equals(
    const char *begin,
    const char *end,
    const char *expected)
{
    size_t length = (size_t)(end - begin);
    size_t expected_length = strlen(expected);

    return length == expected_length &&
        memcmp(begin, expected, length) == 0;
}

static int copy_plain_json_string(
    const char *begin,
    const char *end,
    char *value,
    size_t value_capacity)
{
    const char *cursor;
    size_t value_length;

    if (end - begin < 2 || *begin != '"' || end[-1] != '"')
    {
        return -1;
    }

    ++begin;
    --end;
    value_length = (size_t)(end - begin);
    if (value_length + 1U > value_capacity)
    {
        return -1;
    }

    for (cursor = begin; cursor < end; ++cursor)
    {
        if (*cursor == '\\' || iscntrl((unsigned char)*cursor))
        {
            return -1;
        }
    }

    memcpy(value, begin, value_length);
    value[value_length] = '\0';
    return 0;
}

static int parse_uint64_span(
    const char *begin,
    const char *end,
    uint64_t *value)
{
    const char *cursor;
    uint64_t parsed = 0U;

    if (begin >= end)
    {
        return -1;
    }

    for (cursor = begin; cursor < end; ++cursor)
    {
        uint64_t digit;

        if (!isdigit((unsigned char)*cursor))
        {
            return -1;
        }

        digit = (unsigned char)*cursor - (unsigned char)'0';
        if (parsed > (UINT64_MAX - digit) / 10U)
        {
            return -1;
        }
        parsed = parsed * 10U + digit;
    }

    *value = parsed;
    return 0;
}

static int parse_top_level_request_fields(
    const char *json,
    size_t length,
    char *type,
    size_t type_capacity,
    uint64_t *request_id)
{
    const char *cursor = json;
    const char *end = json + length;
    int found_type = 0;
    int found_request_id = 0;

    json_skip_space(&cursor, end);
    if (cursor >= end || *cursor != '{')
    {
        return -1;
    }
    ++cursor;
    json_skip_space(&cursor, end);

    while (cursor < end && *cursor != '}')
    {
        const char *key_begin;
        const char *key_end;
        const char *value_begin;
        const char *value_end;

        if (*cursor != '"')
        {
            return -1;
        }
        key_begin = cursor + 1;
        if (json_skip_string(&cursor, end) != 0)
        {
            return -1;
        }
        key_end = cursor - 1;

        json_skip_space(&cursor, end);
        if (cursor >= end || *cursor != ':')
        {
            return -1;
        }
        ++cursor;
        json_skip_space(&cursor, end);

        value_begin = cursor;
        if (json_skip_value(&cursor, end, 1U) != 0)
        {
            return -1;
        }
        value_end = cursor;

        if (json_span_equals(key_begin, key_end, "type"))
        {
            if (found_type ||
                copy_plain_json_string(
                    value_begin,
                    value_end,
                    type,
                    type_capacity) != 0)
            {
                return -1;
            }
            found_type = 1;
        }
        else if (json_span_equals(
                     key_begin,
                     key_end,
                     "request_id"))
        {
            if (found_request_id ||
                parse_uint64_span(
                    value_begin,
                    value_end,
                    request_id) != 0)
            {
                return -1;
            }
            found_request_id = 1;
        }

        json_skip_space(&cursor, end);
        if (cursor < end && *cursor == ',')
        {
            ++cursor;
            json_skip_space(&cursor, end);
            continue;
        }
        if (cursor >= end || *cursor != '}')
        {
            return -1;
        }
    }

    return found_type && found_request_id ? 0 : -1;
}

/* 基础协议解析入口，目前只识别 ping；医疗业务请求随后由 clinic_json 继续解析。 */
int clinic_protocol_parse_request(
    const char *line,
    size_t length,
    clinic_request_t *request)
{
    char json[CLINIC_MAX_FRAME_SIZE + 1U];
    char type[32];
    const char *begin;
    const char *end;

    if (line == NULL || request == NULL)
    {
        return CLINIC_PROTOCOL_INVALID_JSON;
    }

    if (length > CLINIC_MAX_FRAME_SIZE)
    {
        return CLINIC_PROTOCOL_MESSAGE_TOO_LARGE;
    }

    if (!json_is_valid_object(line, length))
    {
        return CLINIC_PROTOCOL_INVALID_JSON;
    }

    memcpy(json, line, length);
    json[length] = '\0';

    begin = skip_whitespace(json);
    end = json + length;
    while (end > begin && isspace((unsigned char)end[-1]))
    {
        --end;
    }

    if (begin >= end || *begin != '{' || end[-1] != '}' ||
        strchr(begin, '\n') != NULL || strchr(begin, '\r') != NULL)
    {
        return CLINIC_PROTOCOL_INVALID_JSON;
    }

    if (parse_top_level_request_fields(
            json,
            length,
            type,
            sizeof(type),
            &request->request_id) != 0)
    {
        return CLINIC_PROTOCOL_INVALID_JSON;
    }

    if (strcmp(type, "ping") != 0)
    {
        return CLINIC_PROTOCOL_UNKNOWN_REQUEST;
    }

    request->type = CLINIC_REQUEST_PING;
    return CLINIC_PROTOCOL_OK;
}

/* 编码后的消息自带 '\n'，服务器/客户端据此识别一帧结束。 */
int clinic_protocol_encode_ping(
    uint64_t request_id,
    char *output,
    size_t output_capacity)
{
    if (output == NULL || output_capacity == 0U)
    {
        return -1;
    }

    int written = snprintf(
        output,
        output_capacity,
        "{\"type\":\"ping\",\"request_id\":%" PRIu64 "}\n",
        request_id);

    return written >= 0 && (size_t)written < output_capacity ? written : -1;
}

int clinic_protocol_encode_pong(
    uint64_t request_id,
    char *output,
    size_t output_capacity)
{
    if (output == NULL || output_capacity == 0U)
    {
        return -1;
    }

    int written = snprintf(
        output,
        output_capacity,
        "{\"ok\":true,\"type\":\"pong\",\"request_id\":%" PRIu64 ",\"message\":\"clinic server is alive\"}\n",
        request_id);

    return written >= 0 && (size_t)written < output_capacity ? written : -1;
}

/* 统一错误外壳；只接受无需转义的受控错误码和消息，避免生成非法 JSON。 */
int clinic_protocol_encode_error(
    uint64_t request_id,
    const char *error_code,
    const char *message,
    char *output,
    size_t output_capacity)
{
    if (error_code == NULL || message == NULL ||
        output == NULL || output_capacity == 0U ||
        !json_string_can_encode_without_escaping(error_code) ||
        !json_string_can_encode_without_escaping(message))
    {
        return -1;
    }

    int written = snprintf(
        output,
        output_capacity,
        "{\"ok\":false,\"request_id\":%" PRIu64 ",\"error_code\":\"%s\",\"message\":\"%s\"}\n",
        request_id,
        error_code,
        message);

    return written >= 0 && (size_t)written < output_capacity ? written : -1;
}
