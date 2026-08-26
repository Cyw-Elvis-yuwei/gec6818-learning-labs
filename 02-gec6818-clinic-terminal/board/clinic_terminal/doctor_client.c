/*
 * 文件作用：板端按科室查询医生的业务客户端。
 * 请求携带 request_id 和真实 department_id，响应解析医生 ID、所属科室、姓名、职称
 * 和擅长方向，并拒绝错误类型、越界数量、重复字段和不匹配的 request_id。
 *
 * 本文件不负责医生页面和取号；医生查询只是信息浏览。同步网络接口由工作线程调用，
 * LVGL 主线程回收线程后再创建 doctor_page。
 *
 * 实现顺序：把所选 department_id 编入 list_doctors 请求；收到响应后逐个验证医生记录，
 * 并再次确认每个 doctor.department_id 与请求科室一致，防止跨科室错误数据进入页面。
 */
#define _POSIX_C_SOURCE 200809L

#include "doctor_client.h"

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
    ClinicDoctorListResult *result,
    ClinicDoctorListOutcome outcome,
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

/* department_id 必须为正数；request_id 和 ID 都以精确整数形式写入 JSON。 */
static int encode_doctor_request(
    uint64_t request_id,
    int64_t department_id,
    char *output,
    size_t output_capacity,
    size_t *output_length)
{
    char request_id_text[32];
    char department_id_text[32];
    cJSON *root = NULL;
    char *serialized = NULL;
    size_t serialized_length;
    int written;
    int result = -1;

    if(output == NULL || output_capacity == 0U || output_length == NULL) {
        return -1;
    }
    written = snprintf(
        request_id_text,
        sizeof(request_id_text),
        "%" PRIu64,
        request_id);
    if(written < 0 || (size_t)written >= sizeof(request_id_text)) {
        return -1;
    }
    written = snprintf(
        department_id_text,
        sizeof(department_id_text),
        "%" PRId64,
        department_id);
    if(written < 0 || (size_t)written >= sizeof(department_id_text)) {
        return -1;
    }

    root = cJSON_CreateObject();
    if(root == NULL ||
       cJSON_AddStringToObject(root, "type", "list_doctors") == NULL ||
       cJSON_AddRawToObject(root, "request_id", request_id_text) == NULL ||
       cJSON_AddRawToObject(root, "department_id", department_id_text) == NULL) {
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
    unsigned int seen[5] = {0U, 0U, 0U, 0U, 0U};
    const cJSON *child;
    size_t total = 0U;
    size_t index;

    if(!cJSON_IsObject(object) || field_names == NULL || field_count > 5U) {
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

/* 验证医生数组容量、各文本长度、所属科室和 request_id 后才提交输出。 */
static int decode_doctor_response(
    const char *line,
    size_t line_length,
    uint64_t expected_request_id,
    int64_t expected_department_id,
    ClinicDoctorListResult *result)
{
    static const char *const success_fields[] = {
        "ok", "request_id", "doctors", "message"
    };
    static const char *const error_fields[] = {
        "ok", "request_id", "error_code", "message"
    };
    static const char *const doctor_fields[] = {
        "id", "department_id", "name", "title", "specialty"
    };
    char terminated_line[CLINIC_MAX_FRAME_SIZE + 1U];
    const char *parse_end = NULL;
    cJSON *root = NULL;
    const cJSON *ok_item;
    const cJSON *request_id_item;
    const cJSON *message_item;
    uint64_t response_request_id;
    ClinicDoctorListResult decoded;
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
        const cJSON *doctors_item =
            cJSON_GetObjectItemCaseSensitive(root, "doctors");
        int doctor_count;
        int index;

        if(!object_has_exact_fields(
               root,
               success_fields,
               sizeof(success_fields) / sizeof(success_fields[0])) ||
           !cJSON_IsArray(doctors_item)) {
            goto cleanup;
        }
        doctor_count = cJSON_GetArraySize(doctors_item);
        if(doctor_count < 0 || (size_t)doctor_count > CLINIC_MAX_DOCTORS) {
            goto cleanup;
        }

        for(index = 0; index < doctor_count; ++index) {
            const cJSON *doctor = cJSON_GetArrayItem(doctors_item, index);
            const cJSON *id_item;
            const cJSON *department_id_item;
            const cJSON *name_item;
            const cJSON *title_item;
            const cJSON *specialty_item;

            if(!object_has_exact_fields(
                   doctor,
                   doctor_fields,
                   sizeof(doctor_fields) / sizeof(doctor_fields[0]))) {
                goto cleanup;
            }
            id_item = cJSON_GetObjectItemCaseSensitive(doctor, "id");
            department_id_item =
                cJSON_GetObjectItemCaseSensitive(doctor, "department_id");
            name_item = cJSON_GetObjectItemCaseSensitive(doctor, "name");
            title_item = cJSON_GetObjectItemCaseSensitive(doctor, "title");
            specialty_item =
                cJSON_GetObjectItemCaseSensitive(doctor, "specialty");
            if(json_number_to_positive_int64(
                   id_item,
                   &decoded.doctors[index].id) != 0 ||
               json_number_to_positive_int64(
                   department_id_item,
                   &decoded.doctors[index].department_id) != 0 ||
               decoded.doctors[index].department_id != expected_department_id ||
               !cJSON_IsString(name_item) || name_item->valuestring == NULL ||
               name_item->valuestring[0] == '\0' ||
               !cJSON_IsString(title_item) || title_item->valuestring == NULL ||
               !cJSON_IsString(specialty_item) ||
               specialty_item->valuestring == NULL ||
               copy_text(
                   decoded.doctors[index].name,
                   sizeof(decoded.doctors[index].name),
                   name_item->valuestring,
                   CLINIC_DOCTOR_NAME_MAX_LENGTH) != 0 ||
               copy_text(
                   decoded.doctors[index].title,
                   sizeof(decoded.doctors[index].title),
                   title_item->valuestring,
                   CLINIC_DOCTOR_TITLE_MAX_LENGTH) != 0 ||
               copy_text(
                   decoded.doctors[index].specialty,
                   sizeof(decoded.doctors[index].specialty),
                   specialty_item->valuestring,
                   CLINIC_DOCTOR_SPECIALTY_MAX_LENGTH) != 0) {
                goto cleanup;
            }
        }

        decoded.outcome = CLINIC_DOCTOR_LIST_SUCCESS;
        decoded.doctor_count = (size_t)doctor_count;
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
        decoded.outcome = CLINIC_DOCTOR_LIST_SERVER_ERROR;
    }

    *result = decoded;
    decode_result = 0;

cleanup:
    cJSON_Delete(root);
    clear_memory(terminated_line, sizeof(terminated_line));
    return decode_result;
}

/* 医生查询同步入口，由 main.c 的 doctor_request_worker 调用。 */
int clinic_doctor_list_request(
    const char *server_ip,
    const char *server_port,
    uint64_t request_id,
    int64_t department_id,
    unsigned int timeout_ms,
    ClinicDoctorListResult *result)
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
        CLINIC_DOCTOR_LIST_PROTOCOL_ERROR,
        "invalid doctor request");

    if(server_ip == NULL || server_ip[0] == '\0' ||
       server_port == NULL || server_port[0] == '\0' ||
       request_id == 0U || request_id > JSON_EXACT_INTEGER_MAX ||
       department_id <= 0 ||
       (uint64_t)department_id > JSON_EXACT_INTEGER_MAX ||
       timeout_ms == 0U) {
        return -1;
    }

    if(encode_doctor_request(
           request_id,
           department_id,
           request,
           sizeof(request),
           &request_length) != 0) {
        set_local_result(
            result,
            CLINIC_DOCTOR_LIST_PROTOCOL_ERROR,
            "could not encode doctor request");
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
            CLINIC_DOCTOR_LIST_NETWORK_ERROR,
            "could not initialize network request");
        goto cleanup;
    }
    if(transport_status == CLINIC_BOARD_TRANSPORT_SEND_ERROR) {
        set_local_result(
            result,
            CLINIC_DOCTOR_LIST_NETWORK_ERROR,
            "could not send doctor request");
        goto cleanup;
    }
    if(transport_status == CLINIC_BOARD_TRANSPORT_RECEIVE_ERROR) {
        set_local_result(
            result,
            CLINIC_DOCTOR_LIST_NETWORK_ERROR,
            "could not receive doctor response");
        goto cleanup;
    }
    if(transport_status != CLINIC_BOARD_TRANSPORT_OK ||
       decode_doctor_response(
           response,
           response_length,
           request_id,
           department_id,
           result) != 0) {
        set_local_result(
            result,
            CLINIC_DOCTOR_LIST_PROTOCOL_ERROR,
            "invalid doctor response");
    }

cleanup:
    clear_memory(request, sizeof(request));
    clear_memory(response, sizeof(response));
    return 0;
}
