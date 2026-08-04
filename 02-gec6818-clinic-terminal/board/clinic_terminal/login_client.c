/*
 * 文件作用（答辩）：板端注册和登录业务客户端。
 * 它把用户名、密码和 request_id 编码成严格 JSON，通过 board_transport 发给服务器，
 * 再校验响应字段、request_id 和 user_id，并转换成界面控制器可消费的结果类型。
 *
 * 线程边界：接口是同步阻塞的，只能由 main.c 创建的认证工作线程调用；本文件不创建
 * LVGL 控件，也不访问 SQLite。含密码的临时缓冲区在使用后会主动清零。
 *
 * 实现顺序：encode_auth_request() 生成请求 -> board_transport 完成 TCP 收发 ->
 * decode_auth_response() 严格检查响应 -> clinic_auth_request() 映射网络/协议/业务结果。
 * login/register 两个公开函数只选择不同 type，复用同一条安全链路。
 */
#define _POSIX_C_SOURCE 200809L

#include "login_client.h"

#include "board_transport.h"
#include "clinic_protocol.h"

#include "cJSON.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_EXACT_INTEGER_MAX UINT64_C(9007199254740991)

static void secure_clear(void *memory, size_t length)
{
    volatile unsigned char *cursor = memory;

    while(length > 0U) {
        *cursor = 0U;
        ++cursor;
        --length;
    }
}

static int copy_text(
    char *destination,
    size_t destination_capacity,
    const char *source,
    size_t maximum_length)
{
    size_t length;

    if(destination == NULL || destination_capacity == 0U || source == NULL) {
        return -1;
    }

    length = strlen(source);
    if(length > maximum_length || length + 1U > destination_capacity) {
        destination[0] = '\0';
        return -1;
    }

    memcpy(destination, source, length + 1U);
    return 0;
}

static void set_local_result(
    ClinicLoginResult *result,
    ClinicLoginOutcome outcome,
    const char *message)
{
    result->outcome = outcome;
    result->user_id = 0;
    result->error_code[0] = '\0';
    result->message[0] = '\0';
    if(message != NULL) {
        (void)copy_text(
            result->message,
            sizeof(result->message),
            message,
            CLINIC_MESSAGE_MAX_LENGTH);
    }
}

/* 只接受有界且非空的账号密码，并把 request_id 以精确十进制文本写入 JSON。 */
static int encode_auth_request(
    const char *request_type,
    uint64_t request_id,
    const char *username,
    const char *password,
    char *output,
    size_t output_capacity,
    size_t *output_length)
{
    char request_id_text[32];
    cJSON *root = NULL;
    char *serialized = NULL;
    size_t serialized_length = 0U;
    int result = -1;

    if(request_type == NULL || output == NULL || output_capacity == 0U ||
       output_length == NULL ||
       snprintf(
           request_id_text,
           sizeof(request_id_text),
           "%" PRIu64,
           request_id) < 0) {
        return -1;
    }

    root = cJSON_CreateObject();
    if(root == NULL ||
       cJSON_AddStringToObject(root, "type", request_type) == NULL ||
       cJSON_AddRawToObject(root, "request_id", request_id_text) == NULL ||
       cJSON_AddStringToObject(root, "username", username) == NULL ||
       cJSON_AddStringToObject(root, "password", password) == NULL) {
        goto cleanup;
    }

    serialized = cJSON_PrintUnformatted(root);
    if(serialized == NULL) {
        goto cleanup;
    }

    serialized_length = strlen(serialized);
    if(serialized_length > CLINIC_MAX_FRAME_SIZE ||
       serialized_length + 2U > output_capacity) {
        goto cleanup;
    }

    memcpy(output, serialized, serialized_length);
    output[serialized_length] = '\n';
    output[serialized_length + 1U] = '\0';
    *output_length = serialized_length + 1U;
    result = 0;

cleanup:
    if(serialized != NULL) {
        secure_clear(serialized, serialized_length);
    }
    cJSON_free(serialized);
    cJSON_Delete(root);
    return result;
}

static int object_has_exact_fields(
    const cJSON *object,
    const char *const *field_names,
    size_t field_count)
{
    unsigned int seen[4] = {0U, 0U, 0U, 0U};
    const cJSON *child;
    size_t total = 0U;
    size_t index;

    if(!cJSON_IsObject(object) || field_names == NULL || field_count > 4U) {
        return 0;
    }

    for(child = object->child; child != NULL; child = child->next) {
        int matched = 0;

        if(child->string == NULL) {
            return 0;
        }
        for(index = 0U; index < field_count; ++index) {
            if(strcmp(child->string, field_names[index]) == 0) {
                ++seen[index];
                matched = 1;
                break;
            }
        }
        if(!matched || seen[index] > 1U) {
            return 0;
        }
        ++total;
    }

    if(total != field_count) {
        return 0;
    }
    for(index = 0U; index < field_count; ++index) {
        if(seen[index] != 1U) {
            return 0;
        }
    }
    return 1;
}

static int json_number_to_uint64(const cJSON *item, uint64_t *value)
{
    uint64_t parsed;

    if(!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
       item->valuedouble > (double)JSON_EXACT_INTEGER_MAX) {
        return -1;
    }

    parsed = (uint64_t)item->valuedouble;
    if((double)parsed != item->valuedouble) {
        return -1;
    }
    *value = parsed;
    return 0;
}

static int json_number_to_positive_int64(const cJSON *item, int64_t *value)
{
    uint64_t parsed;

    if(json_number_to_uint64(item, &parsed) != 0 || parsed == 0U ||
       parsed > (uint64_t)INT64_MAX) {
        return -1;
    }
    *value = (int64_t)parsed;
    return 0;
}

/* 响应必须字段集合准确、request_id 与本次请求一致；成功时 user_id 必须为正数。 */
static int decode_auth_response(
    const char *line,
    size_t line_length,
    uint64_t expected_request_id,
    ClinicLoginResult *result)
{
    static const char *const success_fields[] = {
        "ok", "request_id", "user_id", "message"
    };
    static const char *const error_fields[] = {
        "ok", "request_id", "error_code", "message"
    };
    char terminated_line[CLINIC_MAX_FRAME_SIZE + 1U];
    const char *parse_end = NULL;
    cJSON *root = NULL;
    const cJSON *ok_item;
    const cJSON *request_id_item;
    const cJSON *message_item;
    ClinicResponse response;
    int decode_result = -1;

    if(line == NULL || result == NULL || line_length == 0U ||
       line_length > CLINIC_MAX_FRAME_SIZE ||
       memchr(line, '\0', line_length) != NULL) {
        return -1;
    }

    memcpy(terminated_line, line, line_length);
    terminated_line[line_length] = '\0';
    root = cJSON_ParseWithOpts(terminated_line, &parse_end, 1);
    if(root == NULL || parse_end != terminated_line + line_length ||
       !cJSON_IsObject(root)) {
        goto cleanup;
    }

    memset(&response, 0, sizeof(response));
    ok_item = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id_item = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    message_item = cJSON_GetObjectItemCaseSensitive(root, "message");
    if(!cJSON_IsBool(ok_item) ||
       json_number_to_uint64(request_id_item, &response.request_id) != 0 ||
       response.request_id != expected_request_id ||
       !cJSON_IsString(message_item) || message_item->valuestring == NULL ||
       copy_text(
           response.message,
           sizeof(response.message),
           message_item->valuestring,
           CLINIC_MESSAGE_MAX_LENGTH) != 0) {
        goto cleanup;
    }

    if(cJSON_IsTrue(ok_item)) {
        const cJSON *user_id_item =
            cJSON_GetObjectItemCaseSensitive(root, "user_id");

        if(!object_has_exact_fields(
               root,
               success_fields,
               sizeof(success_fields) / sizeof(success_fields[0])) ||
           json_number_to_positive_int64(user_id_item, &response.user_id) != 0) {
            goto cleanup;
        }
        response.ok = 1;
        response.kind = CLINIC_RESPONSE_AUTH;
    }
    else {
        const cJSON *error_code_item =
            cJSON_GetObjectItemCaseSensitive(root, "error_code");

        if(!object_has_exact_fields(
               root,
               error_fields,
               sizeof(error_fields) / sizeof(error_fields[0])) ||
           !cJSON_IsString(error_code_item) ||
           error_code_item->valuestring == NULL ||
           copy_text(
               response.error_code,
               sizeof(response.error_code),
               error_code_item->valuestring,
               CLINIC_ERROR_CODE_MAX_LENGTH) != 0) {
            goto cleanup;
        }
        response.ok = 0;
        response.kind = CLINIC_RESPONSE_NONE;
    }

    if(response.ok && response.kind != CLINIC_RESPONSE_AUTH) {
        goto cleanup;
    }

    result->outcome = response.ok
        ? CLINIC_LOGIN_SUCCESS
        : CLINIC_LOGIN_AUTH_FAILED;
    result->user_id = response.ok ? response.user_id : 0;
    if(copy_text(
           result->message,
           sizeof(result->message),
           response.message,
           CLINIC_MESSAGE_MAX_LENGTH) != 0 ||
       (!response.ok &&
        copy_text(
            result->error_code,
            sizeof(result->error_code),
            response.error_code,
            CLINIC_ERROR_CODE_MAX_LENGTH) != 0)) {
        goto cleanup;
    }
    decode_result = 0;

cleanup:
    cJSON_Delete(root);
    secure_clear(terminated_line, sizeof(terminated_line));
    return decode_result;
}

/* 认证公共流水线；退出前统一清零包含密码的 request/response 临时缓冲区。 */
static int clinic_auth_request(
    const char *request_type,
    const char *server_ip,
    const char *server_port,
    const char *username,
    const char *password,
    uint64_t request_id,
    unsigned int timeout_ms,
    ClinicLoginResult *result)
{
    char request[CLINIC_MAX_FRAME_SIZE + 2U] = {0};
    char response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    size_t request_length = 0U;
    size_t response_length = 0U;
    ClinicBoardTransportStatus transport_status;

    if(result == NULL) {
        return -1;
    }
    memset(result, 0, sizeof(*result));
    set_local_result(
        result,
        CLINIC_LOGIN_PROTOCOL_ERROR,
        "invalid authentication request");

    if(request_type == NULL ||
       (strcmp(request_type, "login") != 0 &&
        strcmp(request_type, "register") != 0) ||
       server_ip == NULL || server_ip[0] == '\0' ||
       server_port == NULL || server_port[0] == '\0' ||
       username == NULL || username[0] == '\0' ||
       password == NULL || password[0] == '\0' ||
       strlen(username) > CLINIC_USERNAME_MAX_LENGTH ||
       strlen(password) > CLINIC_PASSWORD_MAX_LENGTH ||
       request_id > JSON_EXACT_INTEGER_MAX || timeout_ms == 0U) {
        return -1;
    }

    if(encode_auth_request(
           request_type,
           request_id,
           username,
           password,
           request,
           sizeof(request),
           &request_length) != 0) {
        set_local_result(
            result,
            CLINIC_LOGIN_PROTOCOL_ERROR,
            "could not encode authentication request");
        goto cleanup;
    }

    transport_status = clinic_board_transport_exchange(
        server_ip,
        server_port,
        request,
        request_length,
        timeout_ms,
        response,
        sizeof(response),
        &response_length);
    secure_clear(request, sizeof(request));

    if(transport_status == CLINIC_BOARD_TRANSPORT_INITIALIZATION_ERROR) {
        set_local_result(
            result,
            CLINIC_LOGIN_NETWORK_ERROR,
            "could not initialize network request");
        goto cleanup;
    }
    if(transport_status == CLINIC_BOARD_TRANSPORT_SEND_ERROR) {
        set_local_result(
            result,
            CLINIC_LOGIN_NETWORK_ERROR,
            "could not send authentication request");
        goto cleanup;
    }
    if(transport_status == CLINIC_BOARD_TRANSPORT_RECEIVE_ERROR) {
        set_local_result(
            result,
            CLINIC_LOGIN_NETWORK_ERROR,
            "could not receive authentication response");
        goto cleanup;
    }
    if(transport_status != CLINIC_BOARD_TRANSPORT_OK ||
       decode_auth_response(
           response,
           response_length,
           request_id,
           result) != 0) {
        set_local_result(
            result,
            CLINIC_LOGIN_PROTOCOL_ERROR,
            "invalid authentication response");
    }

cleanup:
    secure_clear(request, sizeof(request));
    secure_clear(response, sizeof(response));
    return 0;
}

/* 登录工作线程调用的同步接口。返回 0 表示调用流程完成，具体成败看 result->outcome。 */
int clinic_login_request(
    const char *server_ip,
    const char *server_port,
    const char *username,
    const char *password,
    uint64_t request_id,
    unsigned int timeout_ms,
    ClinicLoginResult *result)
{
    return clinic_auth_request(
        "login",
        server_ip,
        server_port,
        username,
        password,
        request_id,
        timeout_ms,
        result);
}

/* 注册复用认证流水线，但请求 type 和结果枚举与登录分开，避免 UI 混淆。 */
int clinic_register_request(
    const char *server_ip,
    const char *server_port,
    const char *username,
    const char *password,
    uint64_t request_id,
    unsigned int timeout_ms,
    ClinicRegisterResult *result)
{
    ClinicLoginResult auth_result = {0};
    int request_result;

    if(result == NULL) {
        return -1;
    }
    memset(result, 0, sizeof(*result));
    request_result = clinic_auth_request(
        "register",
        server_ip,
        server_port,
        username,
        password,
        request_id,
        timeout_ms,
        &auth_result);
    if(request_result != 0) {
        result->outcome = CLINIC_REGISTER_PROTOCOL_ERROR;
        return request_result;
    }

    switch(auth_result.outcome) {
        case CLINIC_LOGIN_SUCCESS:
            result->outcome = CLINIC_REGISTER_SUCCESS;
            break;
        case CLINIC_LOGIN_AUTH_FAILED:
            result->outcome = CLINIC_REGISTER_REJECTED;
            break;
        case CLINIC_LOGIN_NETWORK_ERROR:
            result->outcome = CLINIC_REGISTER_NETWORK_ERROR;
            break;
        case CLINIC_LOGIN_PROTOCOL_ERROR:
        default:
            result->outcome = CLINIC_REGISTER_PROTOCOL_ERROR;
            break;
    }
    result->user_id = auth_result.user_id;
    if(copy_text(
           result->error_code,
           sizeof(result->error_code),
           auth_result.error_code,
           CLINIC_ERROR_CODE_MAX_LENGTH) != 0 ||
       copy_text(
           result->message,
           sizeof(result->message),
           auth_result.message,
           CLINIC_MESSAGE_MAX_LENGTH) != 0) {
        memset(result, 0, sizeof(*result));
        result->outcome = CLINIC_REGISTER_PROTOCOL_ERROR;
    }
    return 0;
}
