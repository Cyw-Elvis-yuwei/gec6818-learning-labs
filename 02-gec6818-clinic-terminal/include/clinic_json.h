/*
 * 文件作用（答辩）：声明医疗业务 JSON 的严格编解码接口。
 * decode 把一帧 JSON 转为 ClinicRequest，encode 把 ClinicResponse 转为一帧 JSON；
 * 它是 Handler 与结构化业务层之间的协议适配边界。
 */
#ifndef CLINIC_JSON_H
#define CLINIC_JSON_H

#include "clinic_types.h"

#include <stddef.h>

typedef enum ClinicJsonStatus
{
    CLINIC_JSON_OK = 0,
    CLINIC_JSON_INVALID_JSON = -1,
    CLINIC_JSON_INVALID_REQUEST = -2,
    CLINIC_JSON_UNKNOWN_REQUEST = -3,
    CLINIC_JSON_INVALID_ARGUMENT = -4,
    CLINIC_JSON_OUTPUT_TOO_SMALL = -5,
    CLINIC_JSON_NO_MEMORY = -6
} ClinicJsonStatus;

/* 成功时 request 已清零并填入经过类型、范围和字段集合验证的数据。 */
ClinicJsonStatus clinic_json_decode_request(
    const char *json,
    size_t length,
    ClinicRequest *request);

/* 成功时 output_length 给出序列化字节数；容量不足会返回明确状态。 */
ClinicJsonStatus clinic_json_encode_response(
    const ClinicResponse *response,
    char *output,
    size_t output_capacity,
    size_t *output_length);

#endif
