/*
 * 文件作用（答辩）：板端号单业务客户端，负责 create_ticket 和 get_current_ticket。
 * 取号只发送 user_id 与 department_id，不携带 doctor_id；当前号单查询还会严格解析
 * queue_summary，用于显示当前叫号和本人前方 WAITING 人数。
 *
 * 本文件通过 board_transport 同步收发 JSON，把网络错误、协议错误和服务器业务错误
 * 分开返回。它不计算排队规则、不调用 call_next、不操作 LVGL，也不访问 SQLite；
 * 所有真实业务数据和排队统计均来自服务器。
 *
 * 实现顺序：根据操作类型编码 create_ticket/get_current_ticket；公共 exchange 完成收发；
 * parse_ticket_object 验证号单；当前号单响应还必须通过 parse_queue_summary_object；
 * 最后分别映射为 ClinicTicketCreateResult 或 ClinicCurrentTicketResult。
 */
#define _POSIX_C_SOURCE 200809L

#include "ticket_client.h"

#include "board_transport.h"
#include "clinic_protocol.h"

#include "cJSON.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define JSON_EXACT_INTEGER_MAX UINT64_C(9007199254740991)
#define TICKET_FIELD_COUNT 8U

typedef enum TicketExchangeStatus {
    TICKET_EXCHANGE_OK = 0,
    TICKET_EXCHANGE_NETWORK_ERROR,
    TICKET_EXCHANGE_PROTOCOL_ERROR
} TicketExchangeStatus;

typedef struct DecodedTicketResponse {
    int ok;
    ClinicTicket ticket;
    ClinicQueueSummary queue_summary;
    char error_code[CLINIC_ERROR_CODE_MAX_LENGTH + 1U];
    char message[CLINIC_MESSAGE_MAX_LENGTH + 1U];
} DecodedTicketResponse;

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

static void set_create_result(
    ClinicTicketCreateResult *result,
    ClinicTicketCreateOutcome outcome,
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

static void set_current_result(
    ClinicCurrentTicketResult *result,
    ClinicCurrentTicketOutcome outcome,
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

/* create_ticket 要求 user_id+department_id；get_current_ticket 只要求 user_id。 */
static int encode_ticket_request(
    const char *request_type,
    uint64_t request_id,
    int64_t user_id,
    int64_t department_id,
    char *output,
    size_t output_capacity,
    size_t *output_length)
{
    char request_id_text[32];
    char user_id_text[32];
    char department_id_text[32];
    cJSON *root = NULL;
    char *serialized = NULL;
    size_t serialized_length;
    int written;
    int result = -1;

    if(request_type == NULL || request_type[0] == '\0' || output == NULL ||
       output_capacity == 0U || output_length == NULL) {
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
    written = snprintf(user_id_text, sizeof(user_id_text), "%" PRId64, user_id);
    if(written < 0 || (size_t)written >= sizeof(user_id_text)) {
        return -1;
    }
    if(department_id > 0) {
        written = snprintf(
            department_id_text,
            sizeof(department_id_text),
            "%" PRId64,
            department_id);
        if(written < 0 || (size_t)written >= sizeof(department_id_text)) {
            return -1;
        }
    }

    root = cJSON_CreateObject();
    if(root == NULL ||
       cJSON_AddStringToObject(root, "type", request_type) == NULL ||
       cJSON_AddRawToObject(root, "request_id", request_id_text) == NULL ||
       cJSON_AddRawToObject(root, "user_id", user_id_text) == NULL) {
        goto cleanup;
    }
    if(department_id > 0 &&
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
    unsigned int seen[TICKET_FIELD_COUNT] = {0U};
    const cJSON *child;
    size_t total = 0U;
    size_t index;

    if(!cJSON_IsObject(object) || field_names == NULL ||
       field_count > TICKET_FIELD_COUNT) {
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

static int json_number_to_nonnegative_int64(
    const cJSON *item,
    int64_t *value)
{
    uint64_t parsed;

    if(value == NULL || json_number_to_uint64(item, &parsed) != 0 ||
       parsed > (uint64_t)INT64_MAX) {
        return -1;
    }
    *value = (int64_t)parsed;
    return 0;
}

static int parse_ticket_status(
    const cJSON *item,
    ClinicTicketStatus *status)
{
    if(status == NULL || !cJSON_IsString(item) || item->valuestring == NULL) {
        return -1;
    }
    if(strcmp(item->valuestring, "WAITING") == 0) {
        *status = CLINIC_TICKET_WAITING;
    }
    else if(strcmp(item->valuestring, "CALLED") == 0) {
        *status = CLINIC_TICKET_CALLED;
    }
    else if(strcmp(item->valuestring, "COMPLETED") == 0) {
        *status = CLINIC_TICKET_COMPLETED;
    }
    else if(strcmp(item->valuestring, "CANCELLED") == 0) {
        *status = CLINIC_TICKET_CANCELLED;
    }
    else {
        return -1;
    }
    return 0;
}

/* 把 ticket JSON 转成结构体，并校验正 ID、状态文本、日期和时间字段的一致性。 */
static int parse_ticket_object(
    const cJSON *ticket_item,
    int64_t expected_user_id,
    int64_t expected_department_id,
    ClinicTicket *ticket)
{
    static const char *const ticket_fields[] = {
        "id",
        "user_id",
        "department_id",
        "queue_number",
        "status",
        "service_date",
        "created_time",
        "called_time"
    };
    const cJSON *id_item;
    const cJSON *user_id_item;
    const cJSON *department_id_item;
    const cJSON *queue_number_item;
    const cJSON *status_item;
    const cJSON *service_date_item;
    const cJSON *created_time_item;
    const cJSON *called_time_item;
    ClinicTicket decoded = {0};

    if(ticket == NULL ||
       !object_has_exact_fields(
           ticket_item,
           ticket_fields,
           sizeof(ticket_fields) / sizeof(ticket_fields[0]))) {
        return -1;
    }

    id_item = cJSON_GetObjectItemCaseSensitive(ticket_item, "id");
    user_id_item = cJSON_GetObjectItemCaseSensitive(ticket_item, "user_id");
    department_id_item =
        cJSON_GetObjectItemCaseSensitive(ticket_item, "department_id");
    queue_number_item =
        cJSON_GetObjectItemCaseSensitive(ticket_item, "queue_number");
    status_item = cJSON_GetObjectItemCaseSensitive(ticket_item, "status");
    service_date_item =
        cJSON_GetObjectItemCaseSensitive(ticket_item, "service_date");
    created_time_item =
        cJSON_GetObjectItemCaseSensitive(ticket_item, "created_time");
    called_time_item =
        cJSON_GetObjectItemCaseSensitive(ticket_item, "called_time");

    if(json_number_to_positive_int64(id_item, &decoded.id) != 0 ||
       json_number_to_positive_int64(user_id_item, &decoded.user_id) != 0 ||
       json_number_to_positive_int64(
           department_id_item,
           &decoded.department_id) != 0 ||
       json_number_to_positive_int64(
           queue_number_item,
           &decoded.queue_number) != 0 ||
       json_number_to_positive_int64(
           created_time_item,
           &decoded.created_time) != 0 ||
       decoded.user_id != expected_user_id ||
       (expected_department_id > 0 &&
        decoded.department_id != expected_department_id) ||
       parse_ticket_status(status_item, &decoded.status) != 0 ||
       !cJSON_IsString(service_date_item) ||
       service_date_item->valuestring == NULL ||
       strlen(service_date_item->valuestring) != CLINIC_SERVICE_DATE_LENGTH ||
       copy_text(
           decoded.service_date,
           sizeof(decoded.service_date),
           service_date_item->valuestring,
           CLINIC_SERVICE_DATE_LENGTH) != 0) {
        return -1;
    }

    if(cJSON_IsNull(called_time_item)) {
        decoded.called_time = 0;
    }
    else if(json_number_to_positive_int64(
                called_time_item,
                &decoded.called_time) != 0) {
        return -1;
    }

    *ticket = decoded;
    return 0;
}

/* 当前叫号允许 null，内部转换为 0；waiting_ahead_count 必须是非负精确整数。 */
static int parse_queue_summary_object(
    const cJSON *summary_item,
    ClinicQueueSummary *summary)
{
    static const char *const summary_fields[] = {
        "current_called_queue_number",
        "waiting_ahead_count"
    };
    const cJSON *current_called_item;
    const cJSON *waiting_ahead_item;
    ClinicQueueSummary decoded = {0};

    if(summary == NULL ||
       !object_has_exact_fields(
           summary_item,
           summary_fields,
           sizeof(summary_fields) / sizeof(summary_fields[0]))) {
        return -1;
    }

    current_called_item = cJSON_GetObjectItemCaseSensitive(
        summary_item,
        "current_called_queue_number");
    waiting_ahead_item = cJSON_GetObjectItemCaseSensitive(
        summary_item,
        "waiting_ahead_count");
    if(cJSON_IsNull(current_called_item)) {
        decoded.current_called_queue_number = 0;
    }
    else if(json_number_to_positive_int64(
                current_called_item,
                &decoded.current_called_queue_number) != 0) {
        return -1;
    }
    if(json_number_to_nonnegative_int64(
           waiting_ahead_item,
           &decoded.waiting_ahead_count) != 0) {
        return -1;
    }

    *summary = decoded;
    return 0;
}

/* 响应字段集合随操作变化；排队查询成功时 queue_summary 是必需字段。 */
static int decode_ticket_response(
    const char *line,
    size_t line_length,
    uint64_t expected_request_id,
    int64_t expected_user_id,
    int64_t expected_department_id,
    int require_queue_summary,
    DecodedTicketResponse *result)
{
    static const char *const success_fields_without_summary[] = {
        "ok", "request_id", "ticket", "message"
    };
    static const char *const success_fields_with_summary[] = {
        "ok", "request_id", "ticket", "queue_summary", "message"
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
    const cJSON *queue_summary_item;
    const char *const *success_fields;
    size_t success_field_count;
    uint64_t response_request_id;
    DecodedTicketResponse decoded;
    int decode_result = -1;

    if(line == NULL || result == NULL || line_length == 0U ||
       line_length > CLINIC_MAX_FRAME_SIZE ||
       memchr(line, '\0', line_length) != NULL) {
        return -1;
    }

    memset(&decoded, 0, sizeof(decoded));
    success_fields = require_queue_summary
        ? success_fields_with_summary
        : success_fields_without_summary;
    success_field_count = require_queue_summary
        ? sizeof(success_fields_with_summary) /
              sizeof(success_fields_with_summary[0])
        : sizeof(success_fields_without_summary) /
              sizeof(success_fields_without_summary[0]);
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
    queue_summary_item = cJSON_GetObjectItemCaseSensitive(
        root,
        "queue_summary");
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
        const cJSON *ticket_item =
            cJSON_GetObjectItemCaseSensitive(root, "ticket");

        if(!object_has_exact_fields(
               root,
               success_fields,
               success_field_count) ||
           parse_ticket_object(
               ticket_item,
               expected_user_id,
               expected_department_id,
               &decoded.ticket) != 0 ||
           (require_queue_summary &&
            parse_queue_summary_object(
                queue_summary_item,
                &decoded.queue_summary) != 0)) {
            goto cleanup;
        }
        decoded.ok = 1;
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
        decoded.ok = 0;
    }

    *result = decoded;
    decode_result = 0;

cleanup:
    cJSON_Delete(root);
    clear_memory(terminated_line, sizeof(terminated_line));
    return decode_result;
}

/* 号单请求公共链路，明确区分传输失败和 JSON/协议失败，便于页面给出正确提示。 */
static TicketExchangeStatus exchange_ticket_request(
    const char *server_ip,
    const char *server_port,
    const char *request,
    size_t request_length,
    uint64_t request_id,
    int64_t user_id,
    int64_t expected_department_id,
    int require_queue_summary,
    unsigned int timeout_ms,
    DecodedTicketResponse *response_result)
{
    char response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    size_t response_length = 0U;
    ClinicBoardTransportStatus transport_status;
    TicketExchangeStatus status = TICKET_EXCHANGE_NETWORK_ERROR;

    transport_status = clinic_board_transport_exchange(
        server_ip,
        server_port,
        request,
        request_length,
        timeout_ms,
        response,
        sizeof(response),
        &response_length);
    if(transport_status == CLINIC_BOARD_TRANSPORT_INITIALIZATION_ERROR ||
       transport_status == CLINIC_BOARD_TRANSPORT_SEND_ERROR ||
       transport_status == CLINIC_BOARD_TRANSPORT_RECEIVE_ERROR) {
        goto cleanup;
    }
    if(transport_status != CLINIC_BOARD_TRANSPORT_OK ||
       decode_ticket_response(
           response,
           response_length,
           request_id,
           user_id,
           expected_department_id,
           require_queue_summary,
           response_result) != 0) {
        status = TICKET_EXCHANGE_PROTOCOL_ERROR;
        goto cleanup;
    }
    status = TICKET_EXCHANGE_OK;

cleanup:
    clear_memory(response, sizeof(response));
    return status;
}

/* 按科室取号入口；服务器返回原活动号单时也会保留该票据供页面展示。 */
int clinic_ticket_create_request(
    const char *server_ip,
    const char *server_port,
    uint64_t request_id,
    int64_t user_id,
    int64_t department_id,
    unsigned int timeout_ms,
    ClinicTicketCreateResult *result)
{
    char request[CLINIC_MAX_FRAME_SIZE + 2U] = {0};
    size_t request_length = 0U;
    DecodedTicketResponse decoded = {0};
    TicketExchangeStatus exchange_status;

    if(result == NULL) {
        return -1;
    }
    set_create_result(
        result,
        CLINIC_TICKET_CREATE_PROTOCOL_ERROR,
        "invalid ticket request");

    if(server_ip == NULL || server_ip[0] == '\0' ||
       server_port == NULL || server_port[0] == '\0' ||
       request_id == 0U || request_id > JSON_EXACT_INTEGER_MAX ||
       user_id <= 0 || (uint64_t)user_id > JSON_EXACT_INTEGER_MAX ||
       department_id <= 0 ||
       (uint64_t)department_id > JSON_EXACT_INTEGER_MAX ||
       timeout_ms == 0U) {
        return -1;
    }

    if(encode_ticket_request(
           "create_ticket",
           request_id,
           user_id,
           department_id,
           request,
           sizeof(request),
           &request_length) != 0) {
        set_create_result(
            result,
            CLINIC_TICKET_CREATE_PROTOCOL_ERROR,
            "could not encode ticket request");
        goto cleanup;
    }

    exchange_status = exchange_ticket_request(
        server_ip,
        server_port,
        request,
        request_length,
        request_id,
        user_id,
        department_id,
        0,
        timeout_ms,
        &decoded);
    if(exchange_status == TICKET_EXCHANGE_NETWORK_ERROR) {
        set_create_result(
            result,
            CLINIC_TICKET_CREATE_NETWORK_ERROR,
            "ticket request failed");
    }
    else if(exchange_status == TICKET_EXCHANGE_PROTOCOL_ERROR) {
        set_create_result(
            result,
            CLINIC_TICKET_CREATE_PROTOCOL_ERROR,
            "invalid ticket response");
    }
    else {
        memset(result, 0, sizeof(*result));
        if(!decoded.ok) {
            result->outcome = CLINIC_TICKET_CREATE_SERVER_ERROR;
        }
        else if(strcmp(decoded.message, "active ticket retrieved") == 0) {
            result->outcome = CLINIC_TICKET_CREATE_EXISTING;
        }
        else {
            result->outcome = CLINIC_TICKET_CREATE_SUCCESS;
        }
        result->ticket = decoded.ticket;
        (void)copy_text(
            result->error_code,
            sizeof(result->error_code),
            decoded.error_code,
            CLINIC_ERROR_CODE_MAX_LENGTH);
        (void)copy_text(
            result->message,
            sizeof(result->message),
            decoded.message,
            CLINIC_MESSAGE_MAX_LENGTH);
    }

cleanup:
    clear_memory(request, sizeof(request));
    return 0;
}

/* 排队页手动刷新入口；不自动轮询，也不会在这里发起 call_next。 */
int clinic_ticket_get_current_request(
    const char *server_ip,
    const char *server_port,
    uint64_t request_id,
    int64_t user_id,
    unsigned int timeout_ms,
    ClinicCurrentTicketResult *result)
{
    char request[CLINIC_MAX_FRAME_SIZE + 2U] = {0};
    size_t request_length = 0U;
    DecodedTicketResponse decoded = {0};
    TicketExchangeStatus exchange_status;

    if(result == NULL) {
        return -1;
    }
    set_current_result(
        result,
        CLINIC_CURRENT_TICKET_PROTOCOL_ERROR,
        "invalid current ticket request");

    if(server_ip == NULL || server_ip[0] == '\0' ||
       server_port == NULL || server_port[0] == '\0' ||
       request_id == 0U || request_id > JSON_EXACT_INTEGER_MAX ||
       user_id <= 0 || (uint64_t)user_id > JSON_EXACT_INTEGER_MAX ||
       timeout_ms == 0U) {
        return -1;
    }

    if(encode_ticket_request(
           "get_current_ticket",
           request_id,
           user_id,
           0,
           request,
           sizeof(request),
           &request_length) != 0) {
        set_current_result(
            result,
            CLINIC_CURRENT_TICKET_PROTOCOL_ERROR,
            "could not encode current ticket request");
        goto cleanup;
    }

    exchange_status = exchange_ticket_request(
        server_ip,
        server_port,
        request,
        request_length,
        request_id,
        user_id,
        0,
        1,
        timeout_ms,
        &decoded);
    if(exchange_status == TICKET_EXCHANGE_NETWORK_ERROR) {
        set_current_result(
            result,
            CLINIC_CURRENT_TICKET_NETWORK_ERROR,
            "current ticket request failed");
    }
    else if(exchange_status == TICKET_EXCHANGE_PROTOCOL_ERROR) {
        set_current_result(
            result,
            CLINIC_CURRENT_TICKET_PROTOCOL_ERROR,
            "invalid current ticket response");
    }
    else {
        memset(result, 0, sizeof(*result));
        if(decoded.ok) {
            result->outcome = CLINIC_CURRENT_TICKET_SUCCESS;
            result->ticket = decoded.ticket;
            result->queue_summary = decoded.queue_summary;
        }
        else if(strcmp(decoded.error_code, "CURRENT_TICKET_NOT_FOUND") == 0) {
            result->outcome = CLINIC_CURRENT_TICKET_NO_TICKET;
        }
        else {
            result->outcome = CLINIC_CURRENT_TICKET_SERVER_ERROR;
        }
        (void)copy_text(
            result->error_code,
            sizeof(result->error_code),
            decoded.error_code,
            CLINIC_ERROR_CODE_MAX_LENGTH);
        (void)copy_text(
            result->message,
            sizeof(result->message),
            decoded.message,
            CLINIC_MESSAGE_MAX_LENGTH);
    }

cleanup:
    clear_memory(request, sizeof(request));
    return 0;
}
