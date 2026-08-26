#include "clinic_core.h"
#include "clinic_json.h"
#include "clinic_protocol.h"
#include "clinic_server_handler.h"
#include "clinic_store_sqlite.h"

#include <cjson/cJSON.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/admin_data_test.db"
#define TEST_ITEM_COUNT 10U

static int failures = 0;

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if (!(condition))                                                    \
        {                                                                    \
            fprintf(stderr, "FAIL: %s:%d: %s\n",                           \
                    __FILE__, __LINE__, #condition);                         \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static cJSON *unique_item(const cJSON *object, const char *name)
{
    cJSON *child;
    cJSON *match = NULL;
    unsigned int count = 0U;

    for (child = object == NULL ? NULL : object->child;
         child != NULL;
         child = child->next)
    {
        if (child->string != NULL && strcmp(child->string, name) == 0)
        {
            match = child;
            ++count;
        }
    }
    return count == 1U ? match : NULL;
}

static int prepare_data(
    ClinicStore *store,
    int64_t user_ids[TEST_ITEM_COUNT])
{
    size_t index;

    for (index = 0U; index < TEST_ITEM_COUNT; ++index)
    {
        char username[32];
        ClinicTicket ticket;

        (void)snprintf(
            username,
            sizeof(username),
            "admin-user-%02zu",
            index + 1U);
        if (clinic_store_create_user(
                store,
                username,
                "test-password",
                &user_ids[index]) != CLINIC_STORE_OK ||
            clinic_store_create_ticket(
                store,
                user_ids[index],
                1,
                &ticket) != CLINIC_STORE_OK)
        {
            return -1;
        }
    }
    return 0;
}

static void test_store_pagination(
    ClinicStore *store,
    const int64_t user_ids[TEST_ITEM_COUNT])
{
    ClinicUserSummary users[CLINIC_ADMIN_PAGE_MAX_ITEMS];
    ClinicAdminTicketRecord tickets[CLINIC_ADMIN_PAGE_MAX_ITEMS];
    int64_t after_id = 0;
    size_t total_count = 0U;
    size_t count = 0U;
    int has_more = 0;

    do
    {
        ClinicStoreStatus page_status = clinic_store_list_users(
            store,
            after_id,
            users,
            CLINIC_ADMIN_PAGE_MAX_ITEMS,
            &count,
            &has_more);

        CHECK(page_status == CLINIC_STORE_OK);
        CHECK(count > 0U && count <= CLINIC_ADMIN_PAGE_MAX_ITEMS);
        if (page_status != CLINIC_STORE_OK || count == 0U)
        {
            return;
        }
        if (total_count == 0U)
        {
            CHECK(users[0].id == user_ids[0]);
            CHECK(strcmp(users[0].username, "admin-user-01") == 0);
        }
        total_count += count;
        after_id = users[count - 1U].id;
    } while (has_more);
    CHECK(total_count == TEST_ITEM_COUNT);
    CHECK(strcmp(users[count - 1U].username, "admin-user-10") == 0);

    after_id = 0;
    total_count = 0U;
    do
    {
        ClinicStoreStatus page_status = clinic_store_list_tickets(
            store,
            after_id,
            tickets,
            CLINIC_ADMIN_PAGE_MAX_ITEMS,
            &count,
            &has_more);

        CHECK(page_status == CLINIC_STORE_OK);
        CHECK(count > 0U && count <= CLINIC_ADMIN_PAGE_MAX_ITEMS);
        if (page_status != CLINIC_STORE_OK || count == 0U)
        {
            return;
        }
        if (total_count == 0U)
        {
            CHECK(strcmp(tickets[0].username, "admin-user-01") == 0);
            CHECK(strcmp(tickets[0].department_name, "内科") == 0);
        }
        total_count += count;
        after_id = tickets[count - 1U].ticket.id;
    } while (has_more);
    CHECK(total_count == TEST_ITEM_COUNT);
    CHECK(strcmp(tickets[count - 1U].username, "admin-user-10") == 0);
}

static void test_json_requests(void)
{
    static const char VALID_USERS[] =
        "{\"type\":\"admin_list_users\",\"request_id\":91,"
        "\"after_id\":0,\"limit\":3}";
    static const char VALID_TICKETS[] =
        "{\"type\":\"admin_list_tickets\",\"request_id\":92,"
        "\"after_id\":7,\"limit\":1}";
    static const char EXTRA_FIELD[] =
        "{\"type\":\"admin_list_users\",\"request_id\":93,"
        "\"after_id\":0,\"limit\":3,\"password\":\"x\"}";
    static const char TOO_LARGE[] =
        "{\"type\":\"admin_list_tickets\",\"request_id\":94,"
        "\"after_id\":0,\"limit\":4}";
    static const char NEGATIVE_CURSOR[] =
        "{\"type\":\"admin_list_users\",\"request_id\":95,"
        "\"after_id\":-1,\"limit\":3}";
    ClinicRequest request;

    CHECK(clinic_json_decode_request(
              VALID_USERS,
              sizeof(VALID_USERS) - 1U,
              &request) == CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_ADMIN_LIST_USERS);
    CHECK(request.after_id == 0);
    CHECK(request.limit == CLINIC_ADMIN_PAGE_MAX_ITEMS);

    CHECK(clinic_json_decode_request(
              VALID_TICKETS,
              sizeof(VALID_TICKETS) - 1U,
              &request) == CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_ADMIN_LIST_TICKETS);
    CHECK(request.after_id == 7);
    CHECK(request.limit == 1U);

    CHECK(clinic_json_decode_request(
              EXTRA_FIELD,
              sizeof(EXTRA_FIELD) - 1U,
              &request) == CLINIC_JSON_INVALID_REQUEST);
    CHECK(clinic_json_decode_request(
              TOO_LARGE,
              sizeof(TOO_LARGE) - 1U,
              &request) == CLINIC_JSON_INVALID_REQUEST);
    CHECK(clinic_json_decode_request(
              NEGATIVE_CURSOR,
              sizeof(NEGATIVE_CURSOR) - 1U,
              &request) == CLINIC_JSON_INVALID_REQUEST);
}

static void test_core_and_handler(ClinicStore *store)
{
    ClinicCore core;
    ClinicServerHandler handler;
    ClinicRequest request;
    ClinicResponse response;
    char frame[128];
    char output[CLINIC_MAX_FRAME_SIZE + 128U];
    int frame_length;
    size_t output_length = 0U;
    const char *parse_end = NULL;
    cJSON *root;
    cJSON *tickets;
    cJSON *first;

    CHECK(clinic_core_init(&core, store) == 0);
    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_ADMIN_LIST_USERS;
    request.request_id = 700U;
    request.limit = CLINIC_ADMIN_PAGE_MAX_ITEMS;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_ADMIN_USERS);
    CHECK(response.admin_user_count == CLINIC_ADMIN_PAGE_MAX_ITEMS);
    CHECK(response.has_more == 1);

    /* 使用协议常量生成请求，避免测试夹具与分页上限再次发生硬编码漂移。 */
    frame_length = snprintf(
        frame,
        sizeof(frame),
        "{\"type\":\"admin_list_tickets\",\"request_id\":701,"
        "\"after_id\":0,\"limit\":%zu}",
        (size_t)CLINIC_ADMIN_PAGE_MAX_ITEMS);
    CHECK(frame_length > 0 && (size_t)frame_length < sizeof(frame));
    if (frame_length <= 0 || (size_t)frame_length >= sizeof(frame))
    {
        return;
    }

    CHECK(clinic_server_handler_init(&handler, &core) == 0);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              (size_t)frame_length,
              output,
              sizeof(output),
              &output_length) == 0);
    CHECK(output_length > 0U);
    CHECK(output[output_length - 1U] == '\n');
    CHECK(strstr(output, "password") == NULL);
    root = cJSON_ParseWithOpts(output, &parse_end, 1);
    CHECK(cJSON_IsObject(root));
    CHECK(parse_end == output + output_length);
    CHECK(cJSON_IsTrue(unique_item(root, "ok")));
    CHECK(cJSON_IsTrue(unique_item(root, "has_more")));
    tickets = unique_item(root, "tickets");
    CHECK(cJSON_IsArray(tickets));
    CHECK(cJSON_GetArraySize(tickets) ==
          (int)CLINIC_ADMIN_PAGE_MAX_ITEMS);
    first = cJSON_GetArrayItem(tickets, 0);
    CHECK(cJSON_IsString(unique_item(first, "username")));
    CHECK(cJSON_IsString(unique_item(first, "department")));
    CHECK(cJSON_GetObjectItemCaseSensitive(first, "password") == NULL);
    cJSON_Delete(root);
}

static void test_worst_case_response_stays_in_frame(void)
{
    ClinicResponse response;
    char output[CLINIC_MAX_FRAME_SIZE + 128U];
    size_t output_length = 0U;
    size_t index;
    size_t text_index;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_ADMIN_TICKETS;
    response.request_id = 702U;
    response.admin_ticket_count = CLINIC_ADMIN_PAGE_MAX_ITEMS;
    response.has_more = 1;
    (void)snprintf(
        response.message,
        sizeof(response.message),
        "%s",
        "admin tickets retrieved");
    for (index = 0U; index < response.admin_ticket_count; ++index)
    {
        ClinicAdminTicketRecord *record = &response.admin_tickets[index];

        record->ticket.id = (int64_t)index + 1;
        record->ticket.user_id = (int64_t)index + 1;
        record->ticket.department_id = 1;
        record->ticket.queue_number = (int64_t)index + 1;
        record->ticket.status = CLINIC_TICKET_WAITING;
        (void)snprintf(
            record->ticket.service_date,
            sizeof(record->ticket.service_date),
            "%s",
            "2026-07-20");
        record->ticket.created_time = 1700000000;
        for (text_index = 0U;
             text_index < CLINIC_USERNAME_MAX_LENGTH;
             ++text_index)
        {
            record->username[text_index] = '\x01';
        }
        record->username[CLINIC_USERNAME_MAX_LENGTH] = '\0';
        for (text_index = 0U;
             text_index < CLINIC_DEPARTMENT_NAME_MAX_LENGTH;
             ++text_index)
        {
            record->department_name[text_index] = '\x02';
        }
        record->department_name[CLINIC_DEPARTMENT_NAME_MAX_LENGTH] = '\0';
    }
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(output_length <= CLINIC_MAX_FRAME_SIZE);
}

int main(void)
{
    ClinicStore store;
    int64_t user_ids[TEST_ITEM_COUNT] = {0};

    (void)remove(TEST_DATABASE_PATH);
    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(
              &store,
              TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(prepare_data(&store, user_ids) == 0);
    test_store_pagination(&store, user_ids);
    test_json_requests();
    test_core_and_handler(&store);
    test_worst_case_response_stays_in_frame();
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d admin data test(s) failed\n", failures);
        return 1;
    }
    puts("Admin data tests passed");
    return 0;
}
