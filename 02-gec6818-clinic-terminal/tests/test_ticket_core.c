#include "clinic_core.h"
#include "clinic_store_sqlite.h"

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/ticket_core_test.db"

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

static int ticket_is_zeroed(const ClinicTicket *ticket)
{
    ClinicTicket zero_ticket;

    memset(&zero_ticket, 0, sizeof(zero_ticket));
    return memcmp(ticket, &zero_ticket, sizeof(zero_ticket)) == 0;
}

static int queue_summary_is_zeroed(const ClinicQueueSummary *summary)
{
    return summary != NULL &&
        summary->current_called_queue_number == 0 &&
        summary->waiting_ahead_count == 0;
}

static void check_ticket_error(
    const ClinicResponse *response,
    uint64_t request_id,
    const char *error_code)
{
    CHECK(response->ok == 0);
    CHECK(response->kind == CLINIC_RESPONSE_NONE);
    CHECK(response->request_id == request_id);
    CHECK(strcmp(response->error_code, error_code) == 0);
    CHECK(ticket_is_zeroed(&response->ticket));
    CHECK(response->queue_summary_valid == 0);
    CHECK(queue_summary_is_zeroed(&response->queue_summary));
}

static void test_first_ticket_is_returned_by_core(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;
    ClinicTicket first_ticket;
    int64_t user_id = 0;
    int64_t regression_user_id = 0;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "ticket-core-user",
              "teaching-password",
              &user_id) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_CREATE_TICKET;
    request.request_id = 801U;
    request.user_id = user_id;
    request.department_id = 1;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_TICKET);
    CHECK(response.request_id == 801U);
    CHECK(response.ticket.id > 0);
    CHECK(response.ticket.user_id == user_id);
    CHECK(response.ticket.department_id == 1);
    CHECK(response.ticket.queue_number == 1);
    CHECK(response.ticket.status == CLINIC_TICKET_WAITING);
    CHECK(response.ticket.service_date[0] != '\0');
    CHECK(response.ticket.created_time > 0);
    CHECK(response.ticket.called_time == 0);
    CHECK(strcmp(response.message, "ticket created") == 0);
    first_ticket = response.ticket;

    request.request_id = 802U;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_TICKET);
    CHECK(response.request_id == 802U);
    CHECK(response.ticket.id == first_ticket.id);
    CHECK(response.ticket.queue_number == first_ticket.queue_number);
    CHECK(response.ticket.created_time == first_ticket.created_time);
    CHECK(strcmp(response.message, "active ticket retrieved") == 0);

    request.request_id = 803U;
    request.user_id = 999999;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_ticket_error(&response, 803U, "USER_NOT_FOUND");

    request.request_id = 804U;
    request.user_id = user_id;
    request.department_id = 999999;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_ticket_error(&response, 804U, "DEPARTMENT_NOT_FOUND");

    request.request_id = 805U;
    request.user_id = 0;
    request.department_id = 1;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_ticket_error(&response, 805U, "INVALID_ARGUMENT");

    request.request_id = 806U;
    request.user_id = user_id;
    request.department_id = 0;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_ticket_error(&response, 806U, "INVALID_ARGUMENT");

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_REGISTER;
    request.request_id = 807U;
    snprintf(request.username, sizeof(request.username), "%s", "ticket-core-regression");
    snprintf(request.password, sizeof(request.password), "%s", "teaching-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_AUTH);
    CHECK(ticket_is_zeroed(&response.ticket));
    regression_user_id = response.user_id;
    CHECK(regression_user_id > 0);

    request.type = CLINIC_REQ_LOGIN;
    request.request_id = 808U;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_AUTH);
    CHECK(response.user_id == regression_user_id);
    CHECK(ticket_is_zeroed(&response.ticket));

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DEPARTMENTS;
    request.request_id = 809U;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_DEPARTMENTS);
    CHECK(response.department_count == 5U);
    CHECK(ticket_is_zeroed(&response.ticket));

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DOCTORS;
    request.request_id = 810U;
    request.department_id = 1;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_DOCTORS);
    CHECK(response.doctor_count == 2U);
    CHECK(ticket_is_zeroed(&response.ticket));

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_CREATE_TICKET;
    request.request_id = 811U;
    request.user_id = user_id;
    request.department_id = 1;
    memset(&response, 'X', sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_ticket_error(&response, 811U, "DATABASE_ERROR");
}

static int get_ticket_fields_are_equal(
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

static int get_ticket_payload_is_zeroed(const ClinicTicket *ticket)
{
    const unsigned char *bytes = (const unsigned char *)ticket;
    size_t index;

    if (ticket == NULL)
    {
        return 0;
    }
    for (index = 0U; index < sizeof(*ticket); ++index)
    {
        if (bytes[index] != 0U)
        {
            return 0;
        }
    }
    return 1;
}

static void check_get_ticket_failure(
    const ClinicResponse *response,
    uint64_t expected_request_id,
    const char *expected_error_code)
{
    CHECK(response->ok == 0);
    CHECK(response->kind == CLINIC_RESPONSE_NONE);
    CHECK(response->request_id == expected_request_id);
    CHECK(strcmp(response->error_code, expected_error_code) == 0);
    CHECK(get_ticket_payload_is_zeroed(&response->ticket));
    CHECK(response->queue_summary_valid == 0);
    CHECK(queue_summary_is_zeroed(&response->queue_summary));
}

static void test_get_current_ticket_request_is_handled(void)
{
    static const char DATABASE_PATH[] =
        "build/test/ticket_core_get_current_ticket_test.db";
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;
    ClinicTicket first_ticket;
    ClinicTicket second_ticket;
    ClinicTicket other_ticket;
    ClinicTicket called_ticket;
    int64_t user_id = 0;
    int64_t other_user_id = 0;

    (void)remove(DATABASE_PATH);
    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "ticket-core-current-user",
              "teaching-password",
              &user_id) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "ticket-core-current-other",
              "teaching-password",
              &other_user_id) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_GET_CURRENT_TICKET;
    request.request_id = 650U;
    request.user_id = user_id;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_get_ticket_failure(
        &response,
        650U,
        "CURRENT_TICKET_NOT_FOUND");

    CHECK(clinic_store_create_ticket(
              &store,
              user_id,
              1,
              &first_ticket) == CLINIC_STORE_OK);
    request.request_id = 651U;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_TICKET);
    CHECK(response.request_id == 651U);
    CHECK(get_ticket_fields_are_equal(&response.ticket, &first_ticket));
    CHECK(response.queue_summary_valid == 1);
    CHECK(response.queue_summary.current_called_queue_number == 0);
    CHECK(response.queue_summary.waiting_ahead_count == 0);

    CHECK(clinic_store_create_ticket(
              &store,
              other_user_id,
              3,
              &other_ticket) == CLINIC_STORE_OK);
    CHECK(other_ticket.id > first_ticket.id);
    request.request_id = 652U;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(get_ticket_fields_are_equal(&response.ticket, &first_ticket));
    CHECK(response.queue_summary_valid == 1);
    CHECK(response.queue_summary.current_called_queue_number == 0);
    CHECK(response.queue_summary.waiting_ahead_count == 0);

    CHECK(clinic_store_create_ticket(
              &store,
              user_id,
              2,
              &second_ticket) == CLINIC_STORE_OK);
    CHECK(second_ticket.id > other_ticket.id);
    request.request_id = 653U;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_TICKET);
    CHECK(get_ticket_fields_are_equal(&response.ticket, &second_ticket));
    CHECK(response.queue_summary_valid == 1);
    CHECK(response.queue_summary.current_called_queue_number == 0);
    CHECK(response.queue_summary.waiting_ahead_count == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_CALL_NEXT;
    request.request_id = 654U;
    request.department_id = 2;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_TICKET);
    CHECK(response.ticket.id == second_ticket.id);
    CHECK(response.ticket.status == CLINIC_TICKET_CALLED);
    CHECK(response.ticket.called_time > 0);
    called_ticket = response.ticket;

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_GET_CURRENT_TICKET;
    request.request_id = 655U;
    request.user_id = user_id;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_TICKET);
    CHECK(response.request_id == 655U);
    CHECK(get_ticket_fields_are_equal(&response.ticket, &called_ticket));
    CHECK(response.queue_summary_valid == 1);
    CHECK(response.queue_summary.current_called_queue_number == 1);
    CHECK(response.queue_summary.waiting_ahead_count == 0);

    request.request_id = 656U;
    request.user_id = 0;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_get_ticket_failure(&response, 656U, "INVALID_ARGUMENT");

    request.request_id = 657U;
    request.user_id = -1;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_get_ticket_failure(&response, 657U, "INVALID_ARGUMENT");

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    request.request_id = 658U;
    request.user_id = user_id;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_get_ticket_failure(&response, 658U, "DATABASE_ERROR");
    (void)remove(DATABASE_PATH);
}

static void test_get_ticket_request_is_handled(void)
{
    static const char DATABASE_PATH[] =
        "build/test/ticket_core_get_ticket_test.db";
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;
    ClinicTicket created_ticket;
    int64_t user_id = 0;

    (void)remove(DATABASE_PATH);
    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "ticket-core-query-user",
              "teaching-password",
              &user_id) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_ticket(
              &store,
              user_id,
              1,
              &created_ticket) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_GET_TICKET;
    request.request_id = 620U;
    request.ticket_id = created_ticket.id;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_TICKET);
    CHECK(response.request_id == 620U);
    CHECK(get_ticket_fields_are_equal(&response.ticket, &created_ticket));

    request.request_id = 621U;
    request.ticket_id = INT64_MAX;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_get_ticket_failure(&response, 621U, "TICKET_NOT_FOUND");

    request.request_id = 622U;
    request.ticket_id = 0;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_get_ticket_failure(&response, 622U, "INVALID_ARGUMENT");

    request.request_id = 623U;
    request.ticket_id = -1;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_get_ticket_failure(&response, 623U, "INVALID_ARGUMENT");

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    request.request_id = 624U;
    request.ticket_id = created_ticket.id;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_get_ticket_failure(&response, 624U, "DATABASE_ERROR");
    (void)remove(DATABASE_PATH);
}

static void test_call_next_request_is_handled(void)
{
    static const char DATABASE_PATH[] =
        "build/test/ticket_core_call_next_test.db";
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;
    ClinicTicket first_ticket;
    ClinicTicket second_ticket;
    ClinicTicket called_ticket;
    ClinicTicket second_called_ticket;
    int64_t first_user_id = 0;
    int64_t second_user_id = 0;

    (void)remove(DATABASE_PATH);
    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "ticket-core-call-next-one",
              "teaching-password",
              &first_user_id) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "ticket-core-call-next-two",
              "teaching-password",
              &second_user_id) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_ticket(
              &store,
              first_user_id,
              1,
              &first_ticket) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_ticket(
              &store,
              second_user_id,
              1,
              &second_ticket) == CLINIC_STORE_OK);
    CHECK(first_ticket.queue_number == 1);
    CHECK(second_ticket.queue_number == 2);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_CALL_NEXT;
    request.request_id = 701U;
    request.department_id = 1;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_TICKET);
    CHECK(response.request_id == 701U);
    CHECK(response.ticket.id == first_ticket.id);
    CHECK(response.ticket.user_id == first_ticket.user_id);
    CHECK(response.ticket.department_id == first_ticket.department_id);
    CHECK(response.ticket.queue_number == first_ticket.queue_number);
    CHECK(response.ticket.status == CLINIC_TICKET_CALLED);
    CHECK(strcmp(response.ticket.service_date, first_ticket.service_date) == 0);
    CHECK(response.ticket.created_time == first_ticket.created_time);
    CHECK(response.ticket.called_time > 0);
    called_ticket = response.ticket;

    request.request_id = 702U;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_TICKET);
    CHECK(response.request_id == 702U);
    CHECK(response.ticket.id == second_ticket.id);
    CHECK(response.ticket.id != called_ticket.id);
    CHECK(response.ticket.user_id == second_ticket.user_id);
    CHECK(response.ticket.department_id == second_ticket.department_id);
    CHECK(response.ticket.queue_number == second_ticket.queue_number);
    CHECK(response.ticket.status == CLINIC_TICKET_CALLED);
    CHECK(strcmp(response.ticket.service_date, second_ticket.service_date) == 0);
    CHECK(response.ticket.created_time == second_ticket.created_time);
    CHECK(response.ticket.called_time > 0);
    second_called_ticket = response.ticket;

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_GET_TICKET;
    request.request_id = 703U;
    request.ticket_id = first_ticket.id;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_TICKET);
    CHECK(response.request_id == 703U);
    CHECK(get_ticket_fields_are_equal(&response.ticket, &called_ticket));
    CHECK(response.ticket.called_time == called_ticket.called_time);

    request.request_id = 704U;
    request.ticket_id = second_ticket.id;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_TICKET);
    CHECK(response.request_id == 704U);
    CHECK(get_ticket_fields_are_equal(&response.ticket, &second_called_ticket));

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_CALL_NEXT;
    request.request_id = 705U;
    request.department_id = 1;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_ticket_error(&response, 705U, "NO_WAITING_TICKET");

    request.request_id = 706U;
    request.department_id = 3;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_ticket_error(&response, 706U, "NO_WAITING_TICKET");

    request.request_id = 707U;
    request.department_id = 999999;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_ticket_error(&response, 707U, "DEPARTMENT_NOT_FOUND");

    request.request_id = 708U;
    request.department_id = 0;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_ticket_error(&response, 708U, "INVALID_ARGUMENT");

    request.request_id = 709U;
    request.department_id = -1;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_ticket_error(&response, 709U, "INVALID_ARGUMENT");

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    request.request_id = 710U;
    request.department_id = 1;
    memset(&response, 0xA5, sizeof(response));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    check_ticket_error(&response, 710U, "DATABASE_ERROR");
    (void)remove(DATABASE_PATH);
}

int main(void)
{
    test_call_next_request_is_handled();
    test_get_current_ticket_request_is_handled();
    test_get_ticket_request_is_handled();
    (void)remove(TEST_DATABASE_PATH);
    test_first_ticket_is_returned_by_core();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d ticket core test(s) failed\n", failures);
        return 1;
    }
    puts("ticket core tests passed");
    return 0;
}
