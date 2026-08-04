/*
 * 文件作用（答辩）：基础线协议的公共常量和 ping/pong/error 接口声明。
 * 4096 是单条消息上限，8192 是接收缓冲上限；TCP 消息仍以换行符结束。
 * 医疗业务请求类型和结构体位于 clinic_types.h，业务 JSON 转换位于 clinic_json.h。
 */
#ifndef CLINIC_PROTOCOL_H
#define CLINIC_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define CLINIC_MAX_FRAME_SIZE 4096U
#define CLINIC_RECV_BUFFER_SIZE 8192U

typedef enum clinic_request_type
{
    CLINIC_REQUEST_PING = 1
} clinic_request_type_t;

typedef struct clinic_request
{
    clinic_request_type_t type;
    uint64_t request_id;
} clinic_request_t;

typedef enum clinic_protocol_status
{
    CLINIC_PROTOCOL_OK = 0,
    CLINIC_PROTOCOL_INVALID_JSON = -1,
    CLINIC_PROTOCOL_MESSAGE_TOO_LARGE = -2,
    CLINIC_PROTOCOL_UNKNOWN_REQUEST = -3
} clinic_protocol_status_t;

int clinic_protocol_parse_request(
    const char *line,
    size_t length,
    clinic_request_t *request);

int clinic_protocol_encode_ping(
    uint64_t request_id,
    char *output,
    size_t output_capacity);

int clinic_protocol_encode_pong(
    uint64_t request_id,
    char *output,
    size_t output_capacity);

int clinic_protocol_encode_error(
    uint64_t request_id,
    const char *error_code,
    const char *message,
    char *output,
    size_t output_capacity);

#endif
