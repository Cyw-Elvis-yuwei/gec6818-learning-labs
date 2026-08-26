/*
 * 文件作用：板端科室列表业务客户端。
 * 它构造 list_departments JSON 请求，通过 board_transport 收发一条响应，并严格检查
 * ok、request_id、departments 数组、字段类型、数量和额外字段后输出有界科室列表。
 *
 * 本文件只负责协议适配，不创建页面、不调用 LVGL、不访问 SQLite；网络调用由
 * main.c 的科室请求工作线程执行，主线程 join 后才把结果交给科室页面显示。
 *
 * 实现顺序：编码只含 type/request_id 的请求；传输层按换行收一帧；解析器验证成功或
 * 错误响应的精确字段集合，再逐个复制有界 department id/name 到结果数组。
 */
#define _POSIX_C_SOURCE 200809L

#include "department_client.h"

#include "board_transport.h"
#include "clinic_protocol.h"

#include "cJSON.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_EXACT_INTEGER_MAX UINT64_C(9007199254740991)

static void clear_memory(void *memory, size_t length)
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
    ClinicDepartmentListResult *result,
    ClinicDepartmentListOutcome outcome,
    const char *message)
{
    memset(result, 0, sizeof(*result));
    result->outcome = outcome;
    if(message != NULL) {
        (void)copy_text(
            result->message,
            sizeof(result->message),
            message,
            CLINIC_MESSAGE_MAX_LENGTH);
    }
}

/* list_departments 不需要用户或科室参数，只携带 type 与用于配对的 request_id。 */
static int encode_department_request(
    uint64_t request_id,
    char *output,
    size_t output_capacity,
    size_t *output_length)
{
    char request_id_text[32];
    cJSON *root = NULL;
    char *serialized = NULL;
    size_t serialized_length;
    int text_length;
    int result = -1;

    if(output == NULL || output_capacity == 0U || output_length == NULL) {
        return -1;
    }
    text_length = snprintf(
        request_id_text,
        sizeof(request_id_text),
        "%" PRIu64,
        request_id);
    if(text_length < 0 || (size_t)text_length >= sizeof(request_id_text)) {
        return -1;
    }

    root = cJSON_CreateObject();
    if(root == NULL ||
       cJSON_AddStringToObject(root, "type", "list_departments") == NULL ||
       cJSON_AddRawToObject(root, "request_id", request_id_text) == NULL) {
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

    if(value == NULL || !cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
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

    if(value == NULL || json_number_to_uint64(item, &parsed) != 0 ||
       parsed == 0U || parsed > (uint64_t)INT64_MAX) {
        return -1;
    }
    *value = (int64_t)parsed;
    return 0;
}

/* 拒绝超量数组、重复字段、非正科室 ID、过长名称和不匹配的 request_id。 */
static int decode_department_response(
    const char *line,
    size_t line_length,
    uint64_t expected_request_id,
    ClinicDepartmentListResult *result)
{
    static const char *const success_fields[] = {
        "ok", "request_id", "departments", "message"
    };
    static const char *const error_fields[] = {
        "ok", "request_id", "error_code", "message"
    };
    static const char *const department_fields[] = {"id", "name"};
    char terminated_line[CLINIC_MAX_FRAME_SIZE + 1U];
    const char *parse_end = NULL;
    cJSON *root = NULL;
    const cJSON *ok_item;
    const cJSON *request_id_item;
    const cJSON *message_item;
    uint64_t response_request_id;
    ClinicDepartmentListResult decoded;
    int decode_result = -1;

    if(line == NULL || result == NULL || line_length == 0U ||
       line_length > CLINIC_MAX_FRAME_SIZE ||
       memchr(line, '\0', line_length) != NULL) {
        return -1;
    }

    memset(&decoded, 0, sizeof(decoded));
    memcpy(terminated_line, line, line_length);
    terminated_line[line_length] = '\0';
    root = cJSON_ParseWithOpts(terminated_line, &parse_end, 1);
    if(root == NULL || parse_end != terminated_line + line_length ||
       !cJSON_IsObject(root)) {
        goto cleanup;
    }

    ok_item = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id_item = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    message_item = cJSON_GetObjectItemCaseSensitive(root, "message");
    if(!cJSON_IsBool(ok_item) ||
       json_number_to_uint64(request_id_item, &response_request_id) != 0 ||
       response_request_id != expected_request_id ||
       !cJSON_IsString(message_item) || message_item->valuestring == NULL ||
       copy_text(
           decoded.message,
           sizeof(decoded.message),
           message_item->valuestring,
           CLINIC_MESSAGE_MAX_LENGTH) != 0) {
        goto cleanup;
    }

    if(cJSON_IsTrue(ok_item)) {
        const cJSON *departments_item =
            cJSON_GetObjectItemCaseSensitive(root, "departments");
        int department_count;
        int index;

        if(!object_has_exact_fields(
               root,
               success_fields,
               sizeof(success_fields) / sizeof(success_fields[0])) ||
           !cJSON_IsArray(departments_item)) {
            goto cleanup;
        }
        department_count = cJSON_GetArraySize(departments_item);
        if(department_count < 0 ||
           (size_t)department_count > CLINIC_MAX_DEPARTMENTS) {
            goto cleanup;
        }

        for(index = 0; index < department_count; ++index) {
            const cJSON *department =
                cJSON_GetArrayItem(departments_item, index);
            const cJSON *id_item;
            const cJSON *name_item;

            if(!object_has_exact_fields(
                   department,
                   department_fields,
                   sizeof(department_fields) /
                       sizeof(department_fields[0]))) {
                goto cleanup;
            }
            id_item = cJSON_GetObjectItemCaseSensitive(department, "id");
            name_item = cJSON_GetObjectItemCaseSensitive(department, "name");
            if(json_number_to_positive_int64(
                   id_item,
                   &decoded.departments[index].id) != 0 ||
               !cJSON_IsString(name_item) || name_item->valuestring == NULL ||
               name_item->valuestring[0] == '\0' ||
               copy_text(
                   decoded.departments[index].name,
                   sizeof(decoded.departments[index].name),
                   name_item->valuestring,
                   CLINIC_DEPARTMENT_NAME_MAX_LENGTH) != 0) {
                goto cleanup;
            }
        }

        decoded.outcome = CLINIC_DEPARTMENT_LIST_SUCCESS;
        decoded.department_count = (size_t)department_count;
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
           error_code_item->valuestring[0] == '\0' ||
           copy_text(
               decoded.error_code,
               sizeof(decoded.error_code),
               error_code_item->valuestring,
               CLINIC_ERROR_CODE_MAX_LENGTH) != 0) {
            goto cleanup;
        }
        decoded.outcome = CLINIC_DEPARTMENT_LIST_SERVER_ERROR;
    }

    *result = decoded;
    decode_result = 0;

cleanup:
    cJSON_Delete(root);
    clear_memory(terminated_line, sizeof(terminated_line));
    return decode_result;
}

/* 科室查询的同步客户端入口：编码 -> TCP exchange -> 解码 -> 输出结果枚举。 */
int clinic_department_list_request(
    const char *server_ip,
    const char *server_port,
    uint64_t request_id,
    unsigned int timeout_ms,
    ClinicDepartmentListResult *result)
{
    char request[CLINIC_MAX_FRAME_SIZE + 2U] = {0};
    char response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    size_t request_length = 0U;
    size_t response_length = 0U;
    ClinicBoardTransportStatus transport_status;

    if(result == NULL) {
        return -1;
    }
    set_local_result(
        result,
        CLINIC_DEPARTMENT_LIST_PROTOCOL_ERROR,
        "invalid department request");

    if(server_ip == NULL || server_ip[0] == '\0' ||
       server_port == NULL || server_port[0] == '\0' ||
       request_id == 0U || request_id > JSON_EXACT_INTEGER_MAX ||
       timeout_ms == 0U) {
        return -1;
    }

    if(encode_department_request(
           request_id,
           request,
           sizeof(request),
           &request_length) != 0) {
        set_local_result(
            result,
            CLINIC_DEPARTMENT_LIST_PROTOCOL_ERROR,
            "could not encode department request");
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
    clear_memory(request, sizeof(request));

    if(transport_status == CLINIC_BOARD_TRANSPORT_INITIALIZATION_ERROR) {
        set_local_result(
            result,
            CLINIC_DEPARTMENT_LIST_NETWORK_ERROR,
            "could not initialize network request");
        goto cleanup;
    }
    if(transport_status == CLINIC_BOARD_TRANSPORT_SEND_ERROR) {
        set_local_result(
            result,
            CLINIC_DEPARTMENT_LIST_NETWORK_ERROR,
            "could not send department request");
        goto cleanup;
    }
    if(transport_status == CLINIC_BOARD_TRANSPORT_RECEIVE_ERROR) {
        set_local_result(
            result,
            CLINIC_DEPARTMENT_LIST_NETWORK_ERROR,
            "could not receive department response");
        goto cleanup;
    }
    if(transport_status != CLINIC_BOARD_TRANSPORT_OK ||
       decode_department_response(
           response,
           response_length,
           request_id,
           result) != 0) {
        set_local_result(
            result,
            CLINIC_DEPARTMENT_LIST_PROTOCOL_ERROR,
            "invalid department response");
    }

cleanup:
    clear_memory(request, sizeof(request));
    clear_memory(response, sizeof(response));
    return 0;
}
