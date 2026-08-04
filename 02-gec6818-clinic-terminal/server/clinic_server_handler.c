/*
 * 文件作用（答辩）：服务器的协议入口，把“一帧文本”转换成“一帧响应”。
 * 它先识别基础 ping；业务请求则由 clinic_json 严格解码为 ClinicRequest，交给
 * clinic_core_handle()，再把 ClinicResponse 编码成 JSON 返回。
 *
 * Handler 管协议格式，不直接写 SQL，也不负责排队规则。缺字段、重复字段、类型错误、
 * 未知请求和超长响应在这里映射为稳定协议错误，避免非法数据进入业务层。
 *
 * 答辩口诀：Handler 管“格式”。输入是一行 JSON 文本，输出也是一行 JSON 文本；
 * 中间使用 ClinicRequest/ClinicResponse 与 Core 对接，因此 Core 不需要依赖 cJSON。
 */
#include "clinic_server_handler.h"

#include "clinic_json.h"
#include "clinic_protocol.h"

#include <stdint.h>
#include <string.h>

/* 把解析阶段的失败统一封装为带 request_id、error_code 和 message 的响应。 */
static int encode_protocol_error(
    uint64_t request_id,
    const char *error_code,
    const char *message,
    char *output,
    size_t output_capacity,
    size_t *output_length)
{
    char encoded[CLINIC_MAX_FRAME_SIZE + 1U];
    int length = clinic_protocol_encode_error(
        request_id,
        error_code,
        message,
        encoded,
        sizeof(encoded));

    if (length < 0 || (size_t)length + 1U > output_capacity)
    {
        if (output != NULL && output_capacity > 0U)
        {
            output[0] = '\0';
        }
        *output_length = 0U;
        return -1;
    }
    memcpy(output, encoded, (size_t)length + 1U);
    *output_length = (size_t)length;
    return 0;
}

/* Handler 只保存 Core 指针；数据库由 Core 后面的 Store 管理，不属于 Handler。 */
int clinic_server_handler_init(
    ClinicServerHandler *handler,
    ClinicCore *core)
{
    if (handler == NULL || core == NULL)
    {
        return -1;
    }
    handler->core = core;
    return 0;
}

/*
 * 一帧请求的完整协议流程：
 * 1. 检查输入/输出缓冲区；
 * 2. 对基础 ping 协议直接响应；
 * 3. clinic_json_decode_request 严格解码为 ClinicRequest；
 * 4. clinic_core_handle 执行业务并生成 ClinicResponse；
 * 5. clinic_json_encode_response 编回 JSON，并在末尾加入换行供 TCP 分帧。
 * JSON 语法错误与字段错误分别映射为 INVALID_JSON 和 INVALID_REQUEST。
 */
int clinic_server_handler_handle_frame(
    ClinicServerHandler *handler,
    const char *frame,
    size_t frame_length,
    char *output,
    size_t output_capacity,
    size_t *output_length)
{
    clinic_request_t ping_request;
    ClinicRequest business_request;
    ClinicResponse business_response;
    ClinicJsonStatus json_status;
    int protocol_status;
    int encoded_length;

    if (output_length != NULL)
    {
        *output_length = 0U;
    }
    if (output != NULL && output_capacity > 0U)
    {
        output[0] = '\0';
    }
    if (handler == NULL || handler->core == NULL || frame == NULL ||
        output == NULL || output_capacity == 0U || output_length == NULL)
    {
        return -1;
    }

    protocol_status = clinic_protocol_parse_request(
        frame,
        frame_length,
        &ping_request);
    if (protocol_status == CLINIC_PROTOCOL_MESSAGE_TOO_LARGE)
    {
        return encode_protocol_error(
            0U,
            "MESSAGE_TOO_LARGE",
            "message exceeds 4096 bytes",
            output,
            output_capacity,
            output_length);
    }
    if (protocol_status == CLINIC_PROTOCOL_OK &&
        ping_request.type == CLINIC_REQUEST_PING)
    {
        encoded_length = clinic_protocol_encode_pong(
            ping_request.request_id,
            output,
            output_capacity);
        if (encoded_length < 0)
        {
            output[0] = '\0';
            *output_length = 0U;
            return -1;
        }
        *output_length = (size_t)encoded_length;
        return 0;
    }

    json_status = clinic_json_decode_request(
        frame,
        frame_length,
        &business_request);
    if (json_status == CLINIC_JSON_OK)
    {
        if (clinic_core_handle(
                handler->core,
                &business_request,
                &business_response) != 0)
        {
            return -1;
        }
        json_status = clinic_json_encode_response(
            &business_response,
            output,
            output_capacity,
            output_length);
        if (json_status == CLINIC_JSON_OK &&
            *output_length <= CLINIC_MAX_FRAME_SIZE)
        {
            return 0;
        }
        if ((json_status == CLINIC_JSON_OUTPUT_TOO_SMALL &&
             output_capacity > CLINIC_MAX_FRAME_SIZE) ||
            (json_status == CLINIC_JSON_OK &&
             *output_length > CLINIC_MAX_FRAME_SIZE))
        {
            return encode_protocol_error(
                business_response.request_id,
                "RESPONSE_TOO_LARGE",
                "response exceeds server frame capacity",
                output,
                output_capacity,
                output_length);
        }
        return -1;
    }

    if (json_status == CLINIC_JSON_UNKNOWN_REQUEST)
    {
        return encode_protocol_error(
            business_request.request_id,
            "UNKNOWN_REQUEST",
            "unknown request type",
            output,
            output_capacity,
            output_length);
    }
    if (json_status == CLINIC_JSON_INVALID_REQUEST)
    {
        return encode_protocol_error(
            business_request.request_id,
            "INVALID_REQUEST",
            "invalid request fields",
            output,
            output_capacity,
            output_length);
    }
    return encode_protocol_error(
        0U,
        "INVALID_JSON",
        "invalid request json",
        output,
        output_capacity,
        output_length);
}
