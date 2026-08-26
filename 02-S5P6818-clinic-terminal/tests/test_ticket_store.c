#include "clinic_store_sqlite.h"

#include <sqlite3.h>

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/ticket_store_test.db"
#define CURRENT_TICKET_DATABASE_PATH \
    "build/test/current_ticket_store_test.db"

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

static int service_date_is_valid(const char *date)
{
    size_t index;

    if (date == NULL || strlen(date) != CLINIC_SERVICE_DATE_LENGTH ||
        date[4] != '-' || date[7] != '-')
    {
        return 0;
    }
    for (index = 0U; index < CLINIC_SERVICE_DATE_LENGTH; ++index)
    {
        if (index != 4U && index != 7U &&
            (date[index] < '0' || date[index] > '9'))
        {
            return 0;
        }
    }
    return 1;
}

static int tickets_are_equal(
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

static int ticket_is_zeroed(const ClinicTicket *ticket)
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

static int queue_summary_is_zeroed(const ClinicQueueSummary *summary)
{
    return summary != NULL &&
        summary->current_called_queue_number == 0 &&
        summary->waiting_ahead_count == 0;
}

static int set_ticket_service_date(
    const char *database_path,
    int64_t ticket_id,
    const char *service_date)
{
    static const char SQL[] =
        "UPDATE tickets SET service_date=? WHERE id=?;";
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int result;
    int status = -1;

    if (database_path == NULL || ticket_id <= 0 || service_date == NULL)
    {
        return -1;
    }
    result = sqlite3_open_v2(
        database_path,
        &database,
        SQLITE_OPEN_READWRITE,
        NULL);
    if (result != SQLITE_OK)
    {
        goto cleanup;
    }
    result = sqlite3_prepare_v2(database, SQL, -1, &statement, NULL);
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_text(
            statement,
            1,
            service_date,
            -1,
            SQLITE_TRANSIENT);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(
            statement,
            2,
            (sqlite3_int64)ticket_id);
    }
    if (result == SQLITE_OK && sqlite3_step(statement) == SQLITE_DONE &&
        sqlite3_changes(database) == 1)
    {
        status = 0;
    }

cleanup:
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = -1;
    }
    if (database != NULL && sqlite3_close(database) != SQLITE_OK)
    {
        status = -1;
    }
    return status;
}

static int set_ticket_status(
    const char *database_path,
    int64_t ticket_id,
    ClinicTicketStatus ticket_status)
{
    static const char SQL[] =
        "UPDATE tickets SET status=? WHERE id=?;";
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int result;
    int status = -1;

    if (database_path == NULL || ticket_id <= 0 ||
        ticket_status < CLINIC_TICKET_WAITING ||
        ticket_status > CLINIC_TICKET_CANCELLED)
    {
        return -1;
    }
    result = sqlite3_open_v2(
        database_path,
        &database,
        SQLITE_OPEN_READWRITE,
        NULL);
    if (result != SQLITE_OK)
    {
        goto cleanup;
    }
    result = sqlite3_prepare_v2(database, SQL, -1, &statement, NULL);
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int(statement, 1, (int)ticket_status);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(
            statement,
            2,
            (sqlite3_int64)ticket_id);
    }
    if (result == SQLITE_OK && sqlite3_step(statement) == SQLITE_DONE &&
        sqlite3_changes(database) == 1)
    {
        status = 0;
    }

cleanup:
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = -1;
    }
    if (database != NULL && sqlite3_close(database) != SQLITE_OK)
    {
        status = -1;
    }
    return status;
}

static void test_current_ticket_query_rules(void)
{
    ClinicStore store;
    ClinicTicket first_ticket;
    ClinicTicket second_ticket;
    ClinicTicket other_user_ticket;
    ClinicTicket historical_ticket;
    ClinicTicket called_ticket;
    ClinicTicket ahead_ticket;
    ClinicTicket completed_ahead_ticket;
    ClinicTicket completed_ticket;
    ClinicTicket cancelled_ahead_ticket;
    ClinicTicket cancelled_ticket;
    ClinicTicket queried_ticket;
    ClinicQueueSummary queried_summary;
    int64_t user_id = 0;
    int64_t other_user_id = 0;
    int64_t empty_user_id = 0;
    int64_t historical_user_id = 0;
    int64_t completed_ahead_user_id = 0;
    int64_t completed_user_id = 0;
    int64_t cancelled_ahead_user_id = 0;
    int64_t cancelled_user_id = 0;

    (void)remove(CURRENT_TICKET_DATABASE_PATH);
    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(
              &store,
              CURRENT_TICKET_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "current-ticket-user",
              "teaching-password",
              &user_id) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "current-ticket-other-user",
              "teaching-password",
              &other_user_id) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "current-ticket-empty-user",
              "teaching-password",
              &empty_user_id) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "current-ticket-historical-user",
              "teaching-password",
              &historical_user_id) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "current-ticket-completed-ahead",
              "teaching-password",
              &completed_ahead_user_id) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "current-ticket-completed-user",
              "teaching-password",
              &completed_user_id) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "current-ticket-cancelled-ahead",
              "teaching-password",
              &cancelled_ahead_user_id) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "current-ticket-cancelled-user",
              "teaching-password",
              &cancelled_user_id) == CLINIC_STORE_OK);

    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              empty_user_id,
              &queried_ticket,
              &queried_summary) ==
          CLINIC_STORE_CURRENT_TICKET_NOT_FOUND);
    CHECK(ticket_is_zeroed(&queried_ticket));
    CHECK(queue_summary_is_zeroed(&queried_summary));

    CHECK(clinic_store_create_ticket(
               &store,
               user_id,
               1,
               &first_ticket) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_ticket(
              &store,
              empty_user_id,
              1,
              &ahead_ticket) == CLINIC_STORE_OK);
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              user_id,
              &queried_ticket,
              &queried_summary) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &first_ticket));
    CHECK(queried_summary.current_called_queue_number == 0);
    CHECK(queried_summary.waiting_ahead_count == 0);

    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              empty_user_id,
              &queried_ticket,
              &queried_summary) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &ahead_ticket));
    CHECK(queried_summary.current_called_queue_number == 0);
    CHECK(queried_summary.waiting_ahead_count == 1);

    CHECK(clinic_store_create_ticket(
              &store,
              other_user_id,
              3,
              &other_user_ticket) == CLINIC_STORE_OK);
    CHECK(other_user_ticket.id > first_ticket.id);
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              user_id,
              &queried_ticket,
              &queried_summary) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &first_ticket));
    CHECK(queried_summary.current_called_queue_number == 0);
    CHECK(queried_summary.waiting_ahead_count == 0);

    CHECK(clinic_store_create_ticket(
              &store,
              user_id,
              2,
              &second_ticket) == CLINIC_STORE_OK);
    CHECK(second_ticket.id > other_user_ticket.id);
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    CHECK(clinic_store_get_current_ticket(
              &store,
              user_id,
              &queried_ticket,
              &queried_summary) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &second_ticket));
    CHECK(queried_summary.current_called_queue_number == 0);
    CHECK(queried_summary.waiting_ahead_count == 0);

    CHECK(clinic_store_call_next(
              &store,
              1,
              &called_ticket) == CLINIC_STORE_OK);
    CHECK(called_ticket.id == first_ticket.id);
    CHECK(called_ticket.status == CLINIC_TICKET_CALLED);
    CHECK(called_ticket.called_time > 0);
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              empty_user_id,
              &queried_ticket,
              &queried_summary) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &ahead_ticket));
    CHECK(queried_summary.current_called_queue_number == 1);
    CHECK(queried_summary.waiting_ahead_count == 0);

    CHECK(clinic_store_create_ticket(
              &store,
              completed_ahead_user_id,
              5,
              &completed_ahead_ticket) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_ticket(
              &store,
              completed_user_id,
              5,
              &completed_ticket) == CLINIC_STORE_OK);
    CHECK(completed_ahead_ticket.status == CLINIC_TICKET_WAITING);
    CHECK(completed_ticket.queue_number > completed_ahead_ticket.queue_number);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    CHECK(set_ticket_status(
              CURRENT_TICKET_DATABASE_PATH,
              completed_ticket.id,
              CLINIC_TICKET_COMPLETED) == 0);
    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(
              &store,
              CURRENT_TICKET_DATABASE_PATH) == CLINIC_STORE_OK);
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              completed_user_id,
              &queried_ticket,
              &queried_summary) == CLINIC_STORE_OK);
    CHECK(queried_ticket.id == completed_ticket.id);
    CHECK(queried_ticket.status == CLINIC_TICKET_COMPLETED);
    CHECK(queried_summary.current_called_queue_number == 0);
    CHECK(queried_summary.waiting_ahead_count == 0);

    CHECK(clinic_store_create_ticket(
              &store,
              cancelled_ahead_user_id,
              5,
              &cancelled_ahead_ticket) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_ticket(
              &store,
              cancelled_user_id,
              5,
              &cancelled_ticket) == CLINIC_STORE_OK);
    CHECK(cancelled_ahead_ticket.status == CLINIC_TICKET_WAITING);
    CHECK(cancelled_ticket.queue_number > cancelled_ahead_ticket.queue_number);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    CHECK(set_ticket_status(
              CURRENT_TICKET_DATABASE_PATH,
              cancelled_ticket.id,
              CLINIC_TICKET_CANCELLED) == 0);
    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(
              &store,
              CURRENT_TICKET_DATABASE_PATH) == CLINIC_STORE_OK);
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              cancelled_user_id,
              &queried_ticket,
              &queried_summary) == CLINIC_STORE_OK);
    CHECK(queried_ticket.id == cancelled_ticket.id);
    CHECK(queried_ticket.status == CLINIC_TICKET_CANCELLED);
    CHECK(queried_summary.current_called_queue_number == 0);
    CHECK(queried_summary.waiting_ahead_count == 0);

    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              user_id,
              &queried_ticket,
              &queried_summary) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &second_ticket));
    CHECK(queried_summary.current_called_queue_number == 0);
    CHECK(queried_summary.waiting_ahead_count == 0);

    CHECK(clinic_store_call_next(
              &store,
              2,
              &called_ticket) == CLINIC_STORE_OK);
    CHECK(called_ticket.id == second_ticket.id);
    CHECK(called_ticket.status == CLINIC_TICKET_CALLED);
    CHECK(called_ticket.called_time > 0);
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              user_id,
              &queried_ticket,
              &queried_summary) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &called_ticket));
    CHECK(queried_summary.current_called_queue_number == 1);
    CHECK(queried_summary.waiting_ahead_count == 0);

    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              other_user_id,
              &queried_ticket,
              &queried_summary) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &other_user_ticket));
    CHECK(queue_summary_is_zeroed(&queried_summary));

    CHECK(clinic_store_create_ticket(
              &store,
              historical_user_id,
              4,
              &historical_ticket) == CLINIC_STORE_OK);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    CHECK(set_ticket_service_date(
              CURRENT_TICKET_DATABASE_PATH,
              historical_ticket.id,
              "2000-01-01") == 0);

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(
              &store,
              CURRENT_TICKET_DATABASE_PATH) == CLINIC_STORE_OK);
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              historical_user_id,
              &queried_ticket,
              &queried_summary) ==
          CLINIC_STORE_CURRENT_TICKET_NOT_FOUND);
    CHECK(ticket_is_zeroed(&queried_ticket));
    CHECK(queue_summary_is_zeroed(&queried_summary));

    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              999999,
              &queried_ticket,
              &queried_summary) ==
          CLINIC_STORE_CURRENT_TICKET_NOT_FOUND);
    CHECK(ticket_is_zeroed(&queried_ticket));
    CHECK(queue_summary_is_zeroed(&queried_summary));
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              0,
              &queried_ticket,
              &queried_summary) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(ticket_is_zeroed(&queried_ticket));
    CHECK(queue_summary_is_zeroed(&queried_summary));
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              -1,
              &queried_ticket,
              &queried_summary) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(ticket_is_zeroed(&queried_ticket));
    CHECK(queue_summary_is_zeroed(&queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              user_id,
              NULL,
              &queried_summary) ==
          CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_get_current_ticket(
              &store,
              user_id,
              &queried_ticket,
              NULL) ==
          CLINIC_STORE_INVALID_ARGUMENT);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    memset(&queried_summary, 'X', sizeof(queried_summary));
    CHECK(clinic_store_get_current_ticket(
              &store,
              user_id,
              &queried_ticket,
              &queried_summary) == CLINIC_STORE_DATABASE_ERROR);
    CHECK(ticket_is_zeroed(&queried_ticket));
    CHECK(queue_summary_is_zeroed(&queried_summary));
    (void)remove(CURRENT_TICKET_DATABASE_PATH);
}

static void test_ticket_creation_rules_and_existing_data(void)
{
    ClinicStore store;
    ClinicTicket first;
    ClinicTicket duplicate;
    ClinicTicket second;
    ClinicTicket other_department;
    ClinicTicket error_ticket;
    ClinicTicket queried_ticket;
    ClinicTicket called_ticket;
    ClinicTicket called_again;
    ClinicStoredUser stored_user;
    ClinicDepartment departments[CLINIC_MAX_DEPARTMENTS];
    ClinicDoctor doctors[CLINIC_MAX_DOCTORS];
    size_t department_count = 0U;
    size_t doctor_count = 0U;
    int64_t user_one = 0;
    int64_t user_two = 0;
    int64_t user_three = 0;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "ticket-user-one",
              "teaching-password",
              &user_one) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "ticket-user-two",
              "teaching-password",
              &user_two) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "ticket-user-three",
              "teaching-password",
              &user_three) == CLINIC_STORE_OK);

    memset(&first, 0, sizeof(first));
    CHECK(clinic_store_create_ticket(
              &store,
              user_one,
              1,
              &first) == CLINIC_STORE_OK);
    CHECK(first.id > 0);
    CHECK(first.user_id == user_one);
    CHECK(first.department_id == 1);
    CHECK(first.queue_number == 1);
    CHECK(first.status == CLINIC_TICKET_WAITING);
    CHECK(service_date_is_valid(first.service_date));
    CHECK(first.created_time > 0);
    CHECK(first.called_time == 0);

    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    CHECK(clinic_store_get_ticket(
              &store,
              first.id,
              &queried_ticket) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &first));
    CHECK(queried_ticket.status == CLINIC_TICKET_WAITING);
    CHECK(queried_ticket.called_time == 0);

    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    CHECK(clinic_store_get_ticket(
              &store,
              INT64_MAX,
              &queried_ticket) == CLINIC_STORE_TICKET_NOT_FOUND);
    CHECK(ticket_is_zeroed(&queried_ticket));
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    CHECK(clinic_store_get_ticket(
              &store,
              0,
              &queried_ticket) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(ticket_is_zeroed(&queried_ticket));
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    CHECK(clinic_store_get_ticket(
              &store,
              -1,
              &queried_ticket) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(ticket_is_zeroed(&queried_ticket));

    memset(&duplicate, 0, sizeof(duplicate));
    CHECK(clinic_store_create_ticket(
              &store,
              user_one,
              1,
              &duplicate) == CLINIC_STORE_ACTIVE_TICKET_EXISTS);
    CHECK(duplicate.id == first.id);
    CHECK(duplicate.queue_number == first.queue_number);
    CHECK(duplicate.status == first.status);
    CHECK(strcmp(duplicate.service_date, first.service_date) == 0);
    CHECK(duplicate.created_time == first.created_time);
    CHECK(duplicate.called_time == 0);

    memset(&second, 0, sizeof(second));
    CHECK(clinic_store_create_ticket(
              &store,
              user_two,
              1,
              &second) == CLINIC_STORE_OK);
    CHECK(second.queue_number == 2);
    CHECK(strcmp(second.service_date, first.service_date) == 0);

    memset(&other_department, 0, sizeof(other_department));
    CHECK(clinic_store_create_ticket(
              &store,
              user_three,
              2,
              &other_department) == CLINIC_STORE_OK);
    CHECK(other_department.queue_number == 1);
    CHECK(strcmp(other_department.service_date, first.service_date) == 0);

    memset(&error_ticket, 'X', sizeof(error_ticket));
    CHECK(clinic_store_create_ticket(
              &store,
              999999,
              1,
              &error_ticket) == CLINIC_STORE_USER_NOT_FOUND);
    CHECK(error_ticket.id == 0);
    CHECK(clinic_store_create_ticket(
              &store,
              user_one,
              999999,
              &error_ticket) == CLINIC_STORE_DEPARTMENT_NOT_FOUND);
    CHECK(error_ticket.id == 0);
    CHECK(clinic_store_create_ticket(
              &store,
              0,
              1,
              &error_ticket) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_create_ticket(
              &store,
              user_one,
              0,
              &error_ticket) == CLINIC_STORE_INVALID_ARGUMENT);

    memset(&stored_user, 0, sizeof(stored_user));
    CHECK(clinic_store_find_user_by_username(
              &store,
              "ticket-user-one",
              &stored_user) == CLINIC_STORE_OK);
    CHECK(stored_user.id == user_one);
    CHECK(clinic_store_list_departments(
              &store,
              departments,
              CLINIC_MAX_DEPARTMENTS,
              &department_count) == CLINIC_STORE_OK);
    CHECK(department_count == 5U);
    CHECK(strcmp(departments[0].name, "内科") == 0);
    CHECK(clinic_store_list_doctors(
              &store,
              1,
              doctors,
              CLINIC_MAX_DOCTORS,
              &doctor_count) == CLINIC_STORE_OK);
    CHECK(doctor_count == 2U);
    CHECK(strcmp(doctors[0].name, "张医生") == 0);

    memset(&called_ticket, 'X', sizeof(called_ticket));
    CHECK(clinic_store_call_next(
              &store,
              1,
              &called_ticket) == CLINIC_STORE_OK);
    CHECK(called_ticket.id == first.id);
    CHECK(called_ticket.user_id == first.user_id);
    CHECK(called_ticket.department_id == first.department_id);
    CHECK(called_ticket.queue_number == 1);
    CHECK(called_ticket.status == CLINIC_TICKET_CALLED);
    CHECK(strcmp(called_ticket.service_date, first.service_date) == 0);
    CHECK(called_ticket.created_time == first.created_time);
    CHECK(called_ticket.called_time > 0);

    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    CHECK(clinic_store_get_ticket(
              &store,
              first.id,
              &queried_ticket) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &called_ticket));
    CHECK(queried_ticket.status == CLINIC_TICKET_CALLED);
    CHECK(queried_ticket.called_time == called_ticket.called_time);

    memset(&called_again, 'X', sizeof(called_again));
    CHECK(clinic_store_call_next(
              &store,
              1,
              &called_again) == CLINIC_STORE_OK);
    CHECK(called_again.id == second.id);
    CHECK(called_again.id != called_ticket.id);
    CHECK(called_again.user_id == second.user_id);
    CHECK(called_again.department_id == second.department_id);
    CHECK(called_again.queue_number == second.queue_number);
    CHECK(called_again.status == CLINIC_TICKET_CALLED);
    CHECK(strcmp(called_again.service_date, second.service_date) == 0);
    CHECK(called_again.created_time == second.created_time);
    CHECK(called_again.called_time > 0);

    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    CHECK(clinic_store_get_ticket(
              &store,
              first.id,
              &queried_ticket) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &called_ticket));
    CHECK(queried_ticket.called_time == called_ticket.called_time);

    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    CHECK(clinic_store_get_ticket(
              &store,
              second.id,
              &queried_ticket) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &called_again));
    CHECK(queried_ticket.status == CLINIC_TICKET_CALLED);
    CHECK(queried_ticket.called_time == called_again.called_time);

    memset(&error_ticket, 'X', sizeof(error_ticket));
    CHECK(clinic_store_call_next(
              &store,
              1,
              &error_ticket) == CLINIC_STORE_NO_WAITING_TICKET);
    CHECK(ticket_is_zeroed(&error_ticket));

    memset(&error_ticket, 'X', sizeof(error_ticket));
    CHECK(clinic_store_call_next(
              &store,
              3,
              &error_ticket) == CLINIC_STORE_NO_WAITING_TICKET);
    CHECK(ticket_is_zeroed(&error_ticket));
    memset(&error_ticket, 'X', sizeof(error_ticket));
    CHECK(clinic_store_call_next(
              &store,
              999999,
              &error_ticket) == CLINIC_STORE_DEPARTMENT_NOT_FOUND);
    CHECK(ticket_is_zeroed(&error_ticket));
    memset(&error_ticket, 'X', sizeof(error_ticket));
    CHECK(clinic_store_call_next(
              &store,
              0,
              &error_ticket) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(ticket_is_zeroed(&error_ticket));
    memset(&error_ticket, 'X', sizeof(error_ticket));
    CHECK(clinic_store_call_next(
              &store,
              -1,
              &error_ticket) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(ticket_is_zeroed(&error_ticket));
    memset(&error_ticket, 'X', sizeof(error_ticket));
    CHECK(clinic_store_call_next(
              NULL,
              1,
              &error_ticket) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(ticket_is_zeroed(&error_ticket));
    CHECK(clinic_store_call_next(&store, 1, NULL) ==
          CLINIC_STORE_INVALID_ARGUMENT);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    memset(&error_ticket, 'X', sizeof(error_ticket));
    CHECK(clinic_store_create_ticket(
              &store,
              user_one,
              1,
              &error_ticket) == CLINIC_STORE_DATABASE_ERROR);
    CHECK(error_ticket.id == 0);
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    CHECK(clinic_store_get_ticket(
              &store,
              first.id,
              &queried_ticket) == CLINIC_STORE_DATABASE_ERROR);
    CHECK(ticket_is_zeroed(&queried_ticket));
    memset(&error_ticket, 'X', sizeof(error_ticket));
    CHECK(clinic_store_call_next(
              &store,
              1,
              &error_ticket) == CLINIC_STORE_DATABASE_ERROR);
    CHECK(ticket_is_zeroed(&error_ticket));

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    CHECK(clinic_store_get_ticket(
              &store,
              first.id,
              &queried_ticket) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &called_ticket));
    CHECK(queried_ticket.status == CLINIC_TICKET_CALLED);
    CHECK(queried_ticket.called_time == called_ticket.called_time);
    memset(&queried_ticket, 'X', sizeof(queried_ticket));
    CHECK(clinic_store_get_ticket(
              &store,
              second.id,
              &queried_ticket) == CLINIC_STORE_OK);
    CHECK(tickets_are_equal(&queried_ticket, &called_again));
    CHECK(queried_ticket.status == CLINIC_TICKET_CALLED);
    CHECK(queried_ticket.called_time == called_again.called_time);
    memset(&error_ticket, 'X', sizeof(error_ticket));
    CHECK(clinic_store_call_next(
              &store,
              1,
              &error_ticket) == CLINIC_STORE_NO_WAITING_TICKET);
    CHECK(ticket_is_zeroed(&error_ticket));
    memset(&duplicate, 0, sizeof(duplicate));
    CHECK(clinic_store_create_ticket(
              &store,
              user_one,
              1,
              &duplicate) == CLINIC_STORE_ACTIVE_TICKET_EXISTS);
    CHECK(duplicate.id == first.id);
    CHECK(duplicate.queue_number == 1);
    CHECK(duplicate.status == CLINIC_TICKET_CALLED);
    CHECK(duplicate.called_time == called_ticket.called_time);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

int main(void)
{
    test_current_ticket_query_rules();
    (void)remove(TEST_DATABASE_PATH);
    test_ticket_creation_rules_and_existing_data();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d ticket store test(s) failed\n", failures);
        return 1;
    }
    puts("ticket store tests passed");
    return 0;
}
