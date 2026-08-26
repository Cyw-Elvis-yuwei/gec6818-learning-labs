#include "clinic_json.h"

#include <cjson/cJSON.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if (!(condition))                                                    \
        {                                                                    \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static void test_create_ticket_request_is_strictly_decoded(void)
{
    static const char valid[] =
        "{\"type\":\"create_ticket\",\"request_id\":601,"
        "\"user_id\":1,\"department_id\":2}";
    static const char *invalid[] = {
        "{\"type\":\"create_ticket\",\"request_id\":601,\"department_id\":1}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":0,\"department_id\":1}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":-1,\"department_id\":1}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":\"1\",\"department_id\":1}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":true,\"department_id\":1}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1.5,\"department_id\":1}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1e2,\"department_id\":1}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":9223372036854775808,\"department_id\":1}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1,\"user_id\":2,\"department_id\":1}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"payload\":{\"user_id\":1},\"department_id\":1}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1,\"department_id\":0}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1,\"department_id\":-1}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1,\"department_id\":\"1\"}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1,\"department_id\":false}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1,\"department_id\":1.5}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1,\"department_id\":1e2}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1,\"department_id\":9223372036854775808}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1,\"department_id\":1,\"department_id\":2}",
        "{\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1,\"payload\":{\"department_id\":1}}",
        "{\"type\":\"create_ticket\",\"type\":\"create_ticket\",\"request_id\":601,\"user_id\":1,\"department_id\":1}"};
    static const char unknown[] =
        "{\"type\":\"future_ticket\",\"request_id\":601,"
        "\"user_id\":1,\"department_id\":1}";
    ClinicRequest request;
    size_t index;

    memset(&request, 0xA5, sizeof(request));
    CHECK(clinic_json_decode_request(valid, sizeof(valid) - 1U, &request) ==
          CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_CREATE_TICKET);
    CHECK(request.request_id == 601U);
    CHECK(request.user_id == 1);
    CHECK(request.department_id == 2);
    CHECK(request.username[0] == '\0');
    CHECK(request.password[0] == '\0');

    for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
    {
        CHECK(clinic_json_decode_request(
                  invalid[index],
                  strlen(invalid[index]),
                  &request) == CLINIC_JSON_INVALID_REQUEST);
    }
    CHECK(clinic_json_decode_request(unknown, sizeof(unknown) - 1U, &request) ==
          CLINIC_JSON_UNKNOWN_REQUEST);
}

static void fill_ticket_response(
    ClinicResponse *response,
    ClinicTicketStatus status,
    int64_t called_time)
{
    memset(response, 0, sizeof(*response));
    response->ok = 1;
    response->kind = CLINIC_RESPONSE_TICKET;
    response->request_id = 601U;
    response->ticket.id = 1;
    response->ticket.user_id = 1;
    response->ticket.department_id = 1;
    response->ticket.queue_number = 1;
    response->ticket.status = status;
    snprintf(
        response->ticket.service_date,
        sizeof(response->ticket.service_date),
        "%s",
        "2026-07-14");
    response->ticket.created_time = INT64_C(1784000000);
    response->ticket.called_time = called_time;
    snprintf(response->message, sizeof(response->message), "%s", "ticket created");
}

static void check_no_other_payloads(const cJSON *root)
{
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
}

static void test_waiting_and_called_ticket_responses(void)
{
    ClinicResponse response;
    char output[1024] = {0};
    size_t output_length = 0U;
    cJSON *root;
    cJSON *ticket;
    cJSON *item;

    fill_ticket_response(&response, CLINIC_TICKET_WAITING, 0);
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    ticket = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "ticket");
    CHECK(cJSON_IsObject(ticket));
    item = cJSON_GetObjectItemCaseSensitive(ticket, "id");
    CHECK(cJSON_IsNumber(item) && item->valuedouble == 1.0);
    item = cJSON_GetObjectItemCaseSensitive(ticket, "user_id");
    CHECK(cJSON_IsNumber(item) && item->valuedouble == 1.0);
    item = cJSON_GetObjectItemCaseSensitive(ticket, "department_id");
    CHECK(cJSON_IsNumber(item) && item->valuedouble == 1.0);
    item = cJSON_GetObjectItemCaseSensitive(ticket, "queue_number");
    CHECK(cJSON_IsNumber(item) && item->valuedouble == 1.0);
    item = cJSON_GetObjectItemCaseSensitive(ticket, "status");
    CHECK(cJSON_IsString(item) && strcmp(item->valuestring, "WAITING") == 0);
    item = cJSON_GetObjectItemCaseSensitive(ticket, "service_date");
    CHECK(cJSON_IsString(item) && strcmp(item->valuestring, "2026-07-14") == 0);
    item = cJSON_GetObjectItemCaseSensitive(ticket, "created_time");
    CHECK(cJSON_IsNumber(item) && item->valuedouble == 1784000000.0);
    CHECK(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(ticket, "called_time")));
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "queue_summary") == NULL);
    check_no_other_payloads(root);
    cJSON_Delete(root);

    fill_ticket_response(&response, CLINIC_TICKET_CALLED, INT64_C(1784000123));
    snprintf(
        response.ticket.service_date,
        sizeof(response.ticket.service_date),
        "%s",
        "20\"6\\07\n");
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(strstr(output, "\\\"") != NULL);
    CHECK(strstr(output, "\\\\") != NULL);
    CHECK(strstr(output, "\\n") != NULL);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    ticket = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "ticket");
    item = cJSON_GetObjectItemCaseSensitive(ticket, "status");
    CHECK(cJSON_IsString(item) && strcmp(item->valuestring, "CALLED") == 0);
    item = cJSON_GetObjectItemCaseSensitive(ticket, "service_date");
    CHECK(cJSON_IsString(item) && strcmp(item->valuestring, "20\"6\\07\n") == 0);
    item = cJSON_GetObjectItemCaseSensitive(ticket, "called_time");
    CHECK(cJSON_IsNumber(item) && item->valuedouble == 1784000123.0);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "queue_summary") == NULL);
    check_no_other_payloads(root);
    cJSON_Delete(root);
}

static void test_current_ticket_response_with_queue_summary(void)
{
    ClinicResponse response;
    char output[1024] = {0};
    size_t output_length = 0U;
    cJSON *root;
    cJSON *summary;
    cJSON *item;

    fill_ticket_response(&response, CLINIC_TICKET_WAITING, 0);
    response.queue_summary_valid = 1;
    response.queue_summary.current_called_queue_number = 0;
    response.queue_summary.waiting_ahead_count = 2;
    snprintf(
        response.message,
        sizeof(response.message),
        "%s",
        "current ticket retrieved");
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    summary = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "queue_summary");
    CHECK(cJSON_IsObject(summary));
    CHECK(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(
        summary,
        "current_called_queue_number")));
    item = cJSON_GetObjectItemCaseSensitive(summary, "waiting_ahead_count");
    CHECK(cJSON_IsNumber(item) && item->valuedouble == 2.0);
    cJSON_Delete(root);

    response.queue_summary.current_called_queue_number = 7;
    response.queue_summary.waiting_ahead_count = 0;
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    summary = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "queue_summary");
    item = cJSON_GetObjectItemCaseSensitive(
        summary,
        "current_called_queue_number");
    CHECK(cJSON_IsNumber(item) && item->valuedouble == 7.0);
    item = cJSON_GetObjectItemCaseSensitive(summary, "waiting_ahead_count");
    CHECK(cJSON_IsNumber(item) && item->valuedouble == 0.0);
    cJSON_Delete(root);
}

static void test_ticket_boundaries_and_payload_isolation(void)
{
    static const char exact_request_id[] =
        "\"request_id\":18446744073709551615";
    static const char exact_int64[] = "9223372036854775807";
    ClinicResponse response;
    char output[1024] = {0};
    char small_output[16];
    size_t output_length = 0U;
    cJSON *root;

    fill_ticket_response(&response, CLINIC_TICKET_COMPLETED, INT64_MAX);
    response.request_id = UINT64_MAX;
    response.ticket.id = INT64_MAX;
    response.ticket.user_id = INT64_MAX;
    response.ticket.department_id = INT64_MAX;
    response.ticket.queue_number = INT64_MAX;
    response.ticket.created_time = INT64_MAX;
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(strstr(output, exact_request_id) != NULL);
    CHECK(strstr(output, exact_int64) != NULL);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    if (root != NULL)
    {
        check_no_other_payloads(root);
    }
    cJSON_Delete(root);

    response.ticket.status = (ClinicTicketStatus)99;
    memset(output, 'X', sizeof(output));
    output_length = 999U;
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_INVALID_ARGUMENT);
    CHECK(output_length == 0U);
    CHECK(output[0] == '\0');

    fill_ticket_response(&response, CLINIC_TICKET_WAITING, 0);
    response.queue_summary_valid = 1;
    response.queue_summary.current_called_queue_number = -1;
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_INVALID_ARGUMENT);
    response.queue_summary.current_called_queue_number = 0;
    response.queue_summary.waiting_ahead_count = -1;
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_INVALID_ARGUMENT);

    fill_ticket_response(&response, CLINIC_TICKET_CANCELLED, 0);
    memset(small_output, 'X', sizeof(small_output));
    output_length = 999U;
    CHECK(clinic_json_encode_response(
              &response,
              small_output,
              sizeof(small_output),
              &output_length) == CLINIC_JSON_OUTPUT_TOO_SMALL);
    CHECK(output_length == 0U);
    CHECK(small_output[0] == '\0');
    CHECK(small_output[1] == 'X');

    response.kind = CLINIC_RESPONSE_DOCTORS;
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    CHECK(root == NULL || cJSON_GetObjectItemCaseSensitive(root, "ticket") == NULL);
    cJSON_Delete(root);
}

static void test_get_ticket_request_is_strictly_decoded(void)
{
    static const char valid[] =
        "{\"type\":\"get_ticket\",\"request_id\":901,\"ticket_id\":1}";
    static const char *invalid[] = {
        "{\"type\":\"get_ticket\",\"request_id\":901}",
        "{\"type\":\"get_ticket\",\"request_id\":901,\"ticket_id\":0}",
        "{\"type\":\"get_ticket\",\"request_id\":901,\"ticket_id\":-1}",
        "{\"type\":\"get_ticket\",\"request_id\":901,\"ticket_id\":\"1\"}",
        "{\"type\":\"get_ticket\",\"request_id\":901,\"ticket_id\":true}",
        "{\"type\":\"get_ticket\",\"request_id\":901,\"ticket_id\":1.5}",
        "{\"type\":\"get_ticket\",\"request_id\":901,\"ticket_id\":1e2}",
        "{\"type\":\"get_ticket\",\"request_id\":901,\"ticket_id\":9223372036854775808}",
        "{\"type\":\"get_ticket\",\"request_id\":901,\"ticket_id\":1,\"ticket_id\":2}",
        "{\"type\":\"get_ticket\",\"request_id\":901,\"payload\":{\"ticket_id\":1}}"};
    ClinicRequest request;
    size_t index;

    memset(&request, 0xA5, sizeof(request));
    CHECK(clinic_json_decode_request(valid, sizeof(valid) - 1U, &request) ==
          CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_GET_TICKET);
    CHECK(request.request_id == 901U);
    CHECK(request.ticket_id == 1);
    CHECK(request.user_id == 0);
    CHECK(request.department_id == 0);

    for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
    {
        CHECK(clinic_json_decode_request(
                  invalid[index],
                  strlen(invalid[index]),
                  &request) == CLINIC_JSON_INVALID_REQUEST);
    }
}

static void test_get_current_ticket_request_is_strictly_decoded(void)
{
    static const char valid[] =
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":1}";
    static const char *invalid[] = {
        "{\"type\":\"get_current_ticket\",\"request_id\":1201}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":0}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":-1}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":\"1\"}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":true}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":1.5}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":1e2}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":9007199254740992}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":9223372036854775808}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":1,\"user_id\":2}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":1,\"extra\":0}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":1,\"ticket_id\":1}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"payload\":{\"user_id\":1}}",
        "{\"type\":\"get_current_ticket\",\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":1}",
        "{\"type\":\"get_current_ticket\",\"user_id\":1}",
        "{\"type\":\"get_current_ticket\",\"request_id\":0,\"user_id\":1}",
        "{\"type\":\"get_current_ticket\",\"request_id\":-1,\"user_id\":1}",
        "{\"type\":\"get_current_ticket\",\"request_id\":\"1201\",\"user_id\":1}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201.5,\"user_id\":1}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1e2,\"user_id\":1}",
        "{\"type\":\"get_current_ticket\",\"request_id\":9007199254740992,\"user_id\":1}",
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"request_id\":1202,\"user_id\":1}",
        "{\"type\":1,\"request_id\":1201,\"user_id\":1}",
        "{\"type\":\"get_ticket\",\"request_id\":1201,\"user_id\":1}"};
    static const char unknown[] =
        "{\"type\":\"get_current_tickets\",\"request_id\":1201,\"user_id\":1}";
    static const char trailing_garbage[] =
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,\"user_id\":1}x";
    static const char embedded_nul[] =
        "{\"type\":\"get_current_ticket\",\"request_id\":1201,"
        "\"user_id\":1}\0x";
    ClinicRequest request;
    size_t index;

    memset(&request, 0xA5, sizeof(request));
    CHECK(clinic_json_decode_request(valid, sizeof(valid) - 1U, &request) ==
          CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_GET_CURRENT_TICKET);
    CHECK(request.request_id == 1201U);
    CHECK(request.user_id == 1);
    CHECK(request.department_id == 0);
    CHECK(request.ticket_id == 0);

    for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
    {
        CHECK(clinic_json_decode_request(
                  invalid[index],
                  strlen(invalid[index]),
                  &request) == CLINIC_JSON_INVALID_REQUEST);
    }
    CHECK(clinic_json_decode_request(
              unknown,
              sizeof(unknown) - 1U,
              &request) == CLINIC_JSON_UNKNOWN_REQUEST);
    CHECK(clinic_json_decode_request(
              trailing_garbage,
              sizeof(trailing_garbage) - 1U,
              &request) == CLINIC_JSON_INVALID_JSON);
    CHECK(clinic_json_decode_request(
              embedded_nul,
              sizeof(embedded_nul) - 1U,
              &request) == CLINIC_JSON_INVALID_JSON);
}

static void test_call_next_request_is_strictly_decoded(void)
{
    static const char valid[] =
        "{\"type\":\"call_next\",\"request_id\":1001,\"department_id\":1}";
    static const char *invalid[] = {
        "{\"type\":\"call_next\",\"request_id\":1001}",
        "{\"type\":\"call_next\",\"request_id\":1001,\"department_id\":0}",
        "{\"type\":\"call_next\",\"request_id\":1001,\"department_id\":-1}",
        "{\"type\":\"call_next\",\"request_id\":1001,\"department_id\":\"1\"}",
        "{\"type\":\"call_next\",\"request_id\":1001,\"department_id\":true}",
        "{\"type\":\"call_next\",\"request_id\":1001,\"department_id\":1.5}",
        "{\"type\":\"call_next\",\"request_id\":1001,\"department_id\":1e2}",
        "{\"type\":\"call_next\",\"request_id\":1001,\"department_id\":9223372036854775808}",
        "{\"type\":\"call_next\",\"request_id\":1001,\"department_id\":1,\"department_id\":2}",
        "{\"type\":\"call_next\",\"type\":\"call_next\",\"request_id\":1001,\"department_id\":1}",
        "{\"type\":\"call_next\",\"request_id\":1001,\"request_id\":1002,\"department_id\":1}",
        "{\"type\":\"call_next\",\"request_id\":1001,\"payload\":{\"department_id\":1}}",
        "{\"type\":\"call_next\",\"request_id\":1001,\"department_id\":1,\"extra\":0}"};
    static const char trailing_garbage[] =
        "{\"type\":\"call_next\",\"request_id\":1001,\"department_id\":1}x";
    static const char embedded_nul[] =
        "{\"type\":\"call_next\",\"request_id\":1001,"
        "\"department_id\":1}\0x";
    ClinicRequest request;
    size_t index;

    memset(&request, 0xA5, sizeof(request));
    CHECK(clinic_json_decode_request(valid, sizeof(valid) - 1U, &request) ==
          CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_CALL_NEXT);
    CHECK(request.request_id == 1001U);
    CHECK(request.department_id == 1);
    CHECK(request.user_id == 0);
    CHECK(request.ticket_id == 0);

    for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
    {
        CHECK(clinic_json_decode_request(
                  invalid[index],
                  strlen(invalid[index]),
                  &request) == CLINIC_JSON_INVALID_REQUEST);
    }
    CHECK(clinic_json_decode_request(
              trailing_garbage,
              sizeof(trailing_garbage) - 1U,
              &request) == CLINIC_JSON_INVALID_JSON);
    CHECK(clinic_json_decode_request(
              embedded_nul,
              sizeof(embedded_nul) - 1U,
              &request) == CLINIC_JSON_INVALID_JSON);
}

int main(void)
{
    test_call_next_request_is_strictly_decoded();
    test_get_current_ticket_request_is_strictly_decoded();
    test_get_ticket_request_is_strictly_decoded();
    test_create_ticket_request_is_strictly_decoded();
    test_waiting_and_called_ticket_responses();
    test_current_ticket_response_with_queue_summary();
    test_ticket_boundaries_and_payload_isolation();

    if (failures != 0)
    {
        fprintf(stderr, "%d ticket JSON test(s) failed\n", failures);
        return 1;
    }
    puts("ticket JSON tests passed");
    return 0;
}
