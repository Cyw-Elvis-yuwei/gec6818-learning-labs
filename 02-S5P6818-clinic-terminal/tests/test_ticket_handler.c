#include "clinic_server_handler.h"
#include "clinic_store_sqlite.h"
#include "clinic_protocol.h"

#include <cjson/cJSON.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/ticket_handler_test.db"

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

static void check_no_unrelated_payloads(const cJSON *root)
{
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
}

static void check_response_message(
    const char *output,
    const char *expected_message)
{
    cJSON *root = cJSON_Parse(output);
    cJSON *message;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return;
    }
    message = cJSON_GetObjectItemCaseSensitive(root, "message");
    CHECK(cJSON_IsString(message));
    CHECK(message != NULL &&
          strcmp(message->valuestring, expected_message) == 0);
    cJSON_Delete(root);
}

static int64_t register_user(
    ClinicServerHandler *handler,
    char *output,
    size_t output_capacity,
    uint64_t request_id,
    const char *username)
{
    char frame[256];
    size_t output_length = 0U;
    int64_t user_id = 0;
    cJSON *root;
    cJSON *item;
    int frame_length = snprintf(
        frame,
        sizeof(frame),
        "{\"type\":\"register\",\"request_id\":%" PRIu64
        ",\"username\":\"%s\","
        "\"password\":\"teaching-password\"}",
        request_id,
        username);

    CHECK(frame_length > 0);
    CHECK(frame_length > 0 && (size_t)frame_length < sizeof(frame));
    if (frame_length <= 0 || (size_t)frame_length >= sizeof(frame))
    {
        return 0;
    }

    CHECK(clinic_server_handler_handle_frame(
              handler,
              frame,
              (size_t)frame_length,
              output,
              output_capacity,
              &output_length) == 0);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    if (root == NULL)
    {
        return 0;
    }
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    item = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL && item->valuedouble == (double)request_id);
    item = cJSON_GetObjectItemCaseSensitive(root, "user_id");
    CHECK(cJSON_IsNumber(item));
    if (cJSON_IsNumber(item))
    {
        user_id = (int64_t)item->valuedouble;
        CHECK(user_id > 0);
    }
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "ticket") == NULL);
    cJSON_Delete(root);
    return user_id;
}

static int64_t check_ticket_response(
    const char *output,
    size_t output_length,
    uint64_t expected_request_id,
    int64_t expected_user_id,
    int64_t expected_ticket_id)
{
    int64_t ticket_id = 0;
    cJSON *root = cJSON_Parse(output);
    cJSON *ticket;
    cJSON *item;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return 0;
    }
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    item = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL &&
          item->valuedouble == (double)expected_request_id);
    ticket = cJSON_GetObjectItemCaseSensitive(root, "ticket");
    CHECK(cJSON_IsObject(ticket));
    if (cJSON_IsObject(ticket))
    {
        item = cJSON_GetObjectItemCaseSensitive(ticket, "id");
        CHECK(cJSON_IsNumber(item));
        if (cJSON_IsNumber(item))
        {
            ticket_id = (int64_t)item->valuedouble;
            CHECK(ticket_id > 0);
            if (expected_ticket_id > 0)
            {
                CHECK(ticket_id == expected_ticket_id);
            }
        }
        item = cJSON_GetObjectItemCaseSensitive(ticket, "user_id");
        CHECK(cJSON_IsNumber(item));
        CHECK(item != NULL && item->valuedouble == (double)expected_user_id);
        item = cJSON_GetObjectItemCaseSensitive(ticket, "department_id");
        CHECK(cJSON_IsNumber(item));
        CHECK(item != NULL && item->valuedouble == 1.0);
        item = cJSON_GetObjectItemCaseSensitive(ticket, "queue_number");
        CHECK(cJSON_IsNumber(item));
        CHECK(item != NULL && item->valuedouble == 1.0);
        item = cJSON_GetObjectItemCaseSensitive(ticket, "status");
        CHECK(cJSON_IsString(item));
        CHECK(item != NULL && strcmp(item->valuestring, "WAITING") == 0);
        CHECK(cJSON_IsString(
            cJSON_GetObjectItemCaseSensitive(ticket, "service_date")));
        CHECK(cJSON_IsNumber(
            cJSON_GetObjectItemCaseSensitive(ticket, "created_time")));
        CHECK(cJSON_IsNull(
            cJSON_GetObjectItemCaseSensitive(ticket, "called_time")));
    }
    check_no_unrelated_payloads(root);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    cJSON_Delete(root);
    return ticket_id;
}

static int decode_ticket_payload(
    const char *output,
    ClinicTicket *ticket_result)
{
    cJSON *root = NULL;
    cJSON *ticket;
    cJSON *item;
    const char *service_date;
    int status = -1;

    if (output == NULL || ticket_result == NULL)
    {
        return -1;
    }
    memset(ticket_result, 0, sizeof(*ticket_result));
    root = cJSON_Parse(output);
    ticket = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "ticket");
    if (!cJSON_IsObject(ticket))
    {
        goto cleanup;
    }

    item = cJSON_GetObjectItemCaseSensitive(ticket, "id");
    if (!cJSON_IsNumber(item) || item->valuedouble <= 0.0)
    {
        goto cleanup;
    }
    ticket_result->id = (int64_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(ticket, "user_id");
    if (!cJSON_IsNumber(item) || item->valuedouble <= 0.0)
    {
        goto cleanup;
    }
    ticket_result->user_id = (int64_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(ticket, "department_id");
    if (!cJSON_IsNumber(item) || item->valuedouble <= 0.0)
    {
        goto cleanup;
    }
    ticket_result->department_id = (int64_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(ticket, "queue_number");
    if (!cJSON_IsNumber(item) || item->valuedouble <= 0.0)
    {
        goto cleanup;
    }
    ticket_result->queue_number = (int64_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(ticket, "status");
    if (!cJSON_IsString(item))
    {
        goto cleanup;
    }
    if (strcmp(item->valuestring, "WAITING") == 0)
    {
        ticket_result->status = CLINIC_TICKET_WAITING;
    }
    else if (strcmp(item->valuestring, "CALLED") == 0)
    {
        ticket_result->status = CLINIC_TICKET_CALLED;
    }
    else
    {
        goto cleanup;
    }
    item = cJSON_GetObjectItemCaseSensitive(ticket, "service_date");
    if (!cJSON_IsString(item))
    {
        goto cleanup;
    }
    service_date = item->valuestring;
    if (strlen(service_date) != CLINIC_SERVICE_DATE_LENGTH)
    {
        goto cleanup;
    }
    memcpy(
        ticket_result->service_date,
        service_date,
        CLINIC_SERVICE_DATE_LENGTH + 1U);
    item = cJSON_GetObjectItemCaseSensitive(ticket, "created_time");
    if (!cJSON_IsNumber(item) || item->valuedouble <= 0.0)
    {
        goto cleanup;
    }
    ticket_result->created_time = (int64_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(ticket, "called_time");
    if (ticket_result->status == CLINIC_TICKET_WAITING)
    {
        if (!cJSON_IsNull(item))
        {
            goto cleanup;
        }
        ticket_result->called_time = 0;
    }
    else
    {
        if (!cJSON_IsNumber(item) || item->valuedouble <= 0.0 ||
            item->valuedouble >= 9223372036854775808.0)
        {
            goto cleanup;
        }
        ticket_result->called_time = (int64_t)item->valuedouble;
        if ((double)ticket_result->called_time != item->valuedouble)
        {
            goto cleanup;
        }
    }
    status = 0;

cleanup:
    cJSON_Delete(root);
    if (status != 0)
    {
        memset(ticket_result, 0, sizeof(*ticket_result));
    }
    return status;
}

static int ticket_payloads_are_equal(
    const ClinicTicket *left,
    const ClinicTicket *right)
{
    return left != NULL && right != NULL &&
        left->id == right->id &&
        left->user_id == right->user_id &&
        left->department_id == right->department_id &&
        left->queue_number == right->queue_number &&
        left->status == right->status &&
        strcmp(left->service_date, right->service_date) == 0 &&
        left->created_time == right->created_time &&
        left->called_time == right->called_time;
}

static void check_error_response(
    const char *output,
    size_t output_length,
    uint64_t expected_request_id,
    const char *expected_error_code)
{
    cJSON *root = cJSON_Parse(output);
    cJSON *item;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return;
    }
    CHECK(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    item = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL &&
          item->valuedouble == (double)expected_request_id);
    item = cJSON_GetObjectItemCaseSensitive(root, "error_code");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL &&
          strcmp(item->valuestring, expected_error_code) == 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "ticket") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    check_no_unrelated_payloads(root);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    cJSON_Delete(root);
}

static void check_ticket_success_envelope(
    const char *output,
    size_t output_length,
    uint64_t expected_request_id)
{
    cJSON *root = cJSON_Parse(output);
    cJSON *item;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return;
    }
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    item = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL &&
          item->valuedouble == (double)expected_request_id);
    CHECK(cJSON_IsObject(
        cJSON_GetObjectItemCaseSensitive(root, "ticket")));
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    check_no_unrelated_payloads(root);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    cJSON_Delete(root);
}

static void check_current_ticket_summary(
    const char *output,
    int64_t expected_current_called_queue_number,
    int64_t expected_waiting_ahead_count)
{
    cJSON *root = cJSON_Parse(output);
    cJSON *summary;
    cJSON *item;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return;
    }
    summary = cJSON_GetObjectItemCaseSensitive(root, "queue_summary");
    CHECK(cJSON_IsObject(summary));
    if (cJSON_IsObject(summary))
    {
        CHECK(cJSON_GetArraySize(summary) == 2);
        item = cJSON_GetObjectItemCaseSensitive(
            summary,
            "current_called_queue_number");
        if (expected_current_called_queue_number == 0)
        {
            CHECK(cJSON_IsNull(item));
        }
        else
        {
            CHECK(cJSON_IsNumber(item));
            CHECK(item != NULL && item->valuedouble ==
                (double)expected_current_called_queue_number);
        }
        item = cJSON_GetObjectItemCaseSensitive(
            summary,
            "waiting_ahead_count");
        CHECK(cJSON_IsNumber(item));
        CHECK(item != NULL && item->valuedouble ==
            (double)expected_waiting_ahead_count);
    }
    cJSON_Delete(root);
}

static void check_ping_response(
    const char *output,
    size_t output_length,
    uint64_t expected_request_id)
{
    cJSON *root = cJSON_Parse(output);
    cJSON *item;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "type");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, "pong") == 0);
    item = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL &&
          item->valuedouble == (double)expected_request_id);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "ticket") == NULL);
    check_no_unrelated_payloads(root);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    cJSON_Delete(root);
}

static size_t build_create_ticket_frame(
    char *frame,
    size_t capacity,
    uint64_t request_id,
    int64_t user_id,
    int64_t department_id)
{
    int length = snprintf(
        frame,
        capacity,
        "{\"type\":\"create_ticket\",\"request_id\":%" PRIu64
        ",\"user_id\":%" PRId64 ",\"department_id\":%" PRId64 "}",
        request_id,
        user_id,
        department_id);

    CHECK(length > 0);
    CHECK(length > 0 && (size_t)length < capacity);
    if (length <= 0 || (size_t)length >= capacity)
    {
        return 0U;
    }
    return (size_t)length;
}

static size_t build_get_ticket_frame(
    char *frame,
    size_t capacity,
    uint64_t request_id,
    int64_t ticket_id)
{
    int length = snprintf(
        frame,
        capacity,
        "{\"type\":\"get_ticket\",\"request_id\":%" PRIu64
        ",\"ticket_id\":%" PRId64 "}",
        request_id,
        ticket_id);

    CHECK(length > 0);
    CHECK(length > 0 && (size_t)length < capacity);
    if (length <= 0 || (size_t)length >= capacity)
    {
        return 0U;
    }
    return (size_t)length;
}

static size_t build_get_current_ticket_frame(
    char *frame,
    size_t capacity,
    uint64_t request_id,
    int64_t user_id)
{
    int length = snprintf(
        frame,
        capacity,
        "{\"type\":\"get_current_ticket\",\"request_id\":%" PRIu64
        ",\"user_id\":%" PRId64 "}",
        request_id,
        user_id);

    CHECK(length > 0);
    CHECK(length > 0 && (size_t)length < capacity);
    if (length <= 0 || (size_t)length >= capacity)
    {
        return 0U;
    }
    return (size_t)length;
}

static size_t build_call_next_frame(
    char *frame,
    size_t capacity,
    uint64_t request_id,
    int64_t department_id)
{
    int length = snprintf(
        frame,
        capacity,
        "{\"type\":\"call_next\",\"request_id\":%" PRIu64
        ",\"department_id\":%" PRId64 "}",
        request_id,
        department_id);

    CHECK(length > 0);
    CHECK(length > 0 && (size_t)length < capacity);
    if (length <= 0 || (size_t)length >= capacity)
    {
        return 0U;
    }
    return (size_t)length;
}

static void test_ticket_request_is_handled_end_to_end(void)
{
    static const char ping_frame[] =
        "{\"type\":\"ping\",\"request_id\":708}";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char frame[256];
    char oversized_frame[CLINIC_MAX_FRAME_SIZE + 1U];
    char output[2048] = {0};
    size_t frame_length;
    size_t output_length = 0U;
    int64_t user_id;
    int64_t ticket_id;
    ClinicTicket created_ticket;
    ClinicTicket current_ticket;
    ClinicTicket queried_ticket;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_server_handler_init(&handler, &core) == 0);

    user_id = register_user(
        &handler,
        output,
        sizeof(output),
        700U,
        "ticket-handler-user");
    CHECK(user_id > 0);

    frame_length = build_get_current_ticket_frame(
        frame,
        sizeof(frame),
        905U,
        user_id);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(
        output,
        output_length,
        905U,
        "CURRENT_TICKET_NOT_FOUND");

    frame_length = build_create_ticket_frame(
        frame,
        sizeof(frame),
        701U,
        user_id,
        1);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    ticket_id = check_ticket_response(
        output,
        output_length,
        701U,
        user_id,
        0);
    CHECK(decode_ticket_payload(output, &created_ticket) == 0);
    CHECK(created_ticket.id == ticket_id);
    check_response_message(output, "ticket created");

    frame_length = build_get_current_ticket_frame(
        frame,
        sizeof(frame),
        906U,
        user_id);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    CHECK(check_ticket_response(
              output,
              output_length,
              906U,
              user_id,
              ticket_id) == ticket_id);
    check_current_ticket_summary(output, 0, 0);
    CHECK(decode_ticket_payload(output, &current_ticket) == 0);
    CHECK(ticket_payloads_are_equal(&current_ticket, &created_ticket));

    frame_length = build_create_ticket_frame(
        frame,
        sizeof(frame),
        702U,
        user_id,
        1);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    CHECK(check_ticket_response(
              output,
              output_length,
              702U,
              user_id,
              ticket_id) == ticket_id);
    check_response_message(output, "active ticket retrieved");

    frame_length = build_get_ticket_frame(
        frame,
        sizeof(frame),
        902U,
        ticket_id);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    CHECK(check_ticket_response(
              output,
              output_length,
              902U,
              user_id,
              ticket_id) == ticket_id);
    CHECK(decode_ticket_payload(output, &queried_ticket) == 0);
    CHECK(ticket_payloads_are_equal(&queried_ticket, &created_ticket));

    frame_length = build_get_ticket_frame(
        frame,
        sizeof(frame),
        903U,
        INT64_MAX);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 903U, "TICKET_NOT_FOUND");

    frame_length = build_get_ticket_frame(
        frame,
        sizeof(frame),
        904U,
        0);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 904U, "INVALID_REQUEST");

    frame_length = build_create_ticket_frame(
        frame,
        sizeof(frame),
        703U,
        INT64_C(999999),
        1);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 703U, "USER_NOT_FOUND");

    frame_length = build_create_ticket_frame(
        frame,
        sizeof(frame),
        704U,
        user_id,
        INT64_C(999999));
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 704U, "DEPARTMENT_NOT_FOUND");

    frame_length = build_create_ticket_frame(
        frame,
        sizeof(frame),
        705U,
        0,
        1);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 705U, "INVALID_REQUEST");

    frame_length = build_create_ticket_frame(
        frame,
        sizeof(frame),
        706U,
        user_id,
        0);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 706U, "INVALID_REQUEST");

    memset(oversized_frame, ' ', sizeof(oversized_frame));
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              oversized_frame,
              sizeof(oversized_frame),
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 0U, "MESSAGE_TOO_LARGE");

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    frame_length = build_create_ticket_frame(
        frame,
        sizeof(frame),
        707U,
        user_id,
        1);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 707U, "DATABASE_ERROR");

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              ping_frame,
              sizeof(ping_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_ping_response(output, output_length, 708U);
}

static void test_call_next_is_handled_end_to_end(void)
{
    static const char DATABASE_PATH[] =
        "build/test/ticket_handler_call_next_test.db";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char frame[256];
    char output[2048] = {0};
    size_t frame_length;
    size_t output_length = 0U;
    int64_t first_user_id;
    int64_t second_user_id;
    ClinicTicket first_ticket;
    ClinicTicket second_ticket;
    ClinicTicket called_ticket;
    ClinicTicket second_called;
    ClinicTicket current_called;
    ClinicTicket current_second;
    ClinicTicket queried_second;

    (void)remove(DATABASE_PATH);
    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_server_handler_init(&handler, &core) == 0);

    first_user_id = register_user(
        &handler,
        output,
        sizeof(output),
        1101U,
        "ticket-handler-call-next-one");
    second_user_id = register_user(
        &handler,
        output,
        sizeof(output),
        1102U,
        "ticket-handler-call-next-two");
    CHECK(first_user_id > 0);
    CHECK(second_user_id > 0);
    CHECK(first_user_id != second_user_id);

    frame_length = build_create_ticket_frame(
        frame,
        sizeof(frame),
        1103U,
        first_user_id,
        1);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_ticket_success_envelope(output, output_length, 1103U);
    CHECK(decode_ticket_payload(output, &first_ticket) == 0);
    CHECK(first_ticket.user_id == first_user_id);
    CHECK(first_ticket.department_id == 1);
    CHECK(first_ticket.queue_number == 1);
    CHECK(first_ticket.status == CLINIC_TICKET_WAITING);

    frame_length = build_create_ticket_frame(
        frame,
        sizeof(frame),
        1104U,
        second_user_id,
        1);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_ticket_success_envelope(output, output_length, 1104U);
    CHECK(decode_ticket_payload(output, &second_ticket) == 0);
    CHECK(second_ticket.user_id == second_user_id);
    CHECK(second_ticket.department_id == 1);
    CHECK(second_ticket.queue_number == 2);
    CHECK(second_ticket.status == CLINIC_TICKET_WAITING);

    frame_length = build_call_next_frame(frame, sizeof(frame), 1001U, 1);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_ticket_success_envelope(output, output_length, 1001U);
    CHECK(decode_ticket_payload(output, &called_ticket) == 0);
    CHECK(called_ticket.id == first_ticket.id);
    CHECK(called_ticket.user_id == first_ticket.user_id);
    CHECK(called_ticket.department_id == first_ticket.department_id);
    CHECK(called_ticket.queue_number == first_ticket.queue_number);
    CHECK(called_ticket.status == CLINIC_TICKET_CALLED);
    CHECK(strcmp(called_ticket.service_date, first_ticket.service_date) == 0);
    CHECK(called_ticket.created_time == first_ticket.created_time);
    CHECK(called_ticket.called_time > 0);

    frame_length = build_get_current_ticket_frame(
        frame,
        sizeof(frame),
        1007U,
        first_user_id);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_ticket_success_envelope(output, output_length, 1007U);
    check_current_ticket_summary(output, 1, 0);
    CHECK(decode_ticket_payload(output, &current_called) == 0);
    CHECK(ticket_payloads_are_equal(&current_called, &called_ticket));

    frame_length = build_get_current_ticket_frame(
        frame,
        sizeof(frame),
        1008U,
        second_user_id);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_ticket_success_envelope(output, output_length, 1008U);
    check_current_ticket_summary(output, 1, 0);
    CHECK(decode_ticket_payload(output, &current_second) == 0);
    CHECK(ticket_payloads_are_equal(&current_second, &second_ticket));

    frame_length = build_call_next_frame(frame, sizeof(frame), 1002U, 1);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_ticket_success_envelope(output, output_length, 1002U);
    CHECK(decode_ticket_payload(output, &second_called) == 0);
    CHECK(second_called.id == second_ticket.id);
    CHECK(second_called.id != called_ticket.id);
    CHECK(second_called.user_id == second_ticket.user_id);
    CHECK(second_called.department_id == second_ticket.department_id);
    CHECK(second_called.queue_number == second_ticket.queue_number);
    CHECK(second_called.status == CLINIC_TICKET_CALLED);
    CHECK(strcmp(second_called.service_date, second_ticket.service_date) == 0);
    CHECK(second_called.created_time == second_ticket.created_time);
    CHECK(second_called.called_time > 0);

    frame_length = build_get_current_ticket_frame(
        frame,
        sizeof(frame),
        1009U,
        second_user_id);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_ticket_success_envelope(output, output_length, 1009U);
    check_current_ticket_summary(output, 2, 0);

    frame_length = build_get_ticket_frame(
        frame,
        sizeof(frame),
        1003U,
        second_ticket.id);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_ticket_success_envelope(output, output_length, 1003U);
    CHECK(decode_ticket_payload(output, &queried_second) == 0);
    CHECK(ticket_payloads_are_equal(&queried_second, &second_called));
    CHECK(queried_second.status == CLINIC_TICKET_CALLED);
    CHECK(queried_second.called_time == second_called.called_time);

    frame_length = build_call_next_frame(frame, sizeof(frame), 1004U, 1);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 1004U, "NO_WAITING_TICKET");

    frame_length = build_call_next_frame(
        frame,
        sizeof(frame),
        1005U,
        INT64_C(999999));
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 1005U, "DEPARTMENT_NOT_FOUND");

    frame_length = build_call_next_frame(frame, sizeof(frame), 1006U, 0);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 1006U, "INVALID_REQUEST");

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    (void)remove(DATABASE_PATH);
}

int main(void)
{
    test_call_next_is_handled_end_to_end();
    (void)remove(TEST_DATABASE_PATH);
    test_ticket_request_is_handled_end_to_end();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d ticket handler test(s) failed\n", failures);
        return 1;
    }
    puts("ticket handler tests passed");
    return 0;
}
