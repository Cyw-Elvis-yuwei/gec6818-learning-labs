#include "clinic_core.h"
#include "clinic_store_sqlite.h"

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/department_core_test.db"

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

static ClinicStoreStatus report_capacity_exceeded(
    void *context,
    ClinicDepartment *departments,
    size_t capacity,
    size_t *count)
{
    (void)context;
    (void)departments;
    (void)capacity;
    *count = 0U;
    return CLINIC_STORE_CAPACITY_EXCEEDED;
}

static void test_departments_can_be_listed(void)
{
    static const char *expected_names[] = {
        "内科", "外科", "儿科", "眼科", "口腔科"
    };
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;
    size_t index;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DEPARTMENTS;
    request.request_id = 201U;

    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_DEPARTMENTS);
    CHECK(response.request_id == request.request_id);
    CHECK(response.department_count == 5U);
    for (index = 0U; index < response.department_count && index < 5U; ++index)
    {
        CHECK(response.departments[index].id == (int64_t)(index + 1U));
        CHECK(strcmp(response.departments[index].name, expected_names[index]) == 0);
    }

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_maximum_request_id_is_preserved(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DEPARTMENTS;
    request.request_id = UINT64_MAX;

    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_DEPARTMENTS);
    CHECK(response.request_id == UINT64_MAX);
    CHECK(response.department_count == 5U);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_closed_database_fails_without_stale_departments(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DEPARTMENTS;
    request.request_id = 202U;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.department_count == 5U);
    CHECK(response.departments[0].id == 1);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);

    request.request_id = 203U;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(response.kind == CLINIC_RESPONSE_NONE);
    CHECK(response.request_id == request.request_id);
    CHECK(strcmp(response.error_code, "DATABASE_ERROR") == 0);
    CHECK(response.department_count == 0U);
    CHECK(response.departments[0].id == 0);
    CHECK(response.departments[0].name[0] == '\0');
}

static void test_store_capacity_failure_maps_to_internal_error(void)
{
    static const ClinicStoreOperations operations = {
        .list_departments = report_capacity_exceeded};
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    store.operations = &operations;
    store.context = &store;
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DEPARTMENTS;
    request.request_id = 204U;

    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(response.kind == CLINIC_RESPONSE_NONE);
    CHECK(response.request_id == request.request_id);
    CHECK(strcmp(response.error_code, "INTERNAL_ERROR") == 0);
    CHECK(response.department_count == 0U);
}

static void test_department_and_auth_requests_do_not_pollute_each_other(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;
    int64_t registered_user_id;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DEPARTMENTS;
    request.request_id = 205U;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_DEPARTMENTS);
    CHECK(response.department_count == 5U);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_REGISTER;
    request.request_id = 206U;
    snprintf(request.username, sizeof(request.username), "%s", "department-core-user");
    snprintf(request.password, sizeof(request.password), "%s", "teaching-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_AUTH);
    CHECK(response.request_id == request.request_id);
    CHECK(response.user_id > 0);
    CHECK(response.department_count == 0U);
    CHECK(response.departments[0].id == 0);
    registered_user_id = response.user_id;

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DEPARTMENTS;
    request.request_id = 207U;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_DEPARTMENTS);
    CHECK(response.request_id == request.request_id);
    CHECK(response.user_id == 0);
    CHECK(response.department_count == 5U);
    CHECK(response.departments[4].id == 5);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LOGIN;
    request.request_id = 208U;
    snprintf(request.username, sizeof(request.username), "%s", "department-core-user");
    snprintf(request.password, sizeof(request.password), "%s", "teaching-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_AUTH);
    CHECK(response.request_id == request.request_id);
    CHECK(response.user_id == registered_user_id);
    CHECK(response.department_count == 0U);
    CHECK(response.departments[0].id == 0);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_unknown_request_behavior_is_unchanged(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = (ClinicRequestType)999;
    request.request_id = 209U;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(response.kind == CLINIC_RESPONSE_NONE);
    CHECK(response.request_id == request.request_id);
    CHECK(strcmp(response.error_code, "UNKNOWN_REQUEST") == 0);
    CHECK(response.department_count == 0U);
    CHECK(response.departments[0].id == 0);
}

int main(void)
{
    (void)remove(TEST_DATABASE_PATH);
    test_departments_can_be_listed();
    test_maximum_request_id_is_preserved();
    test_closed_database_fails_without_stale_departments();
    test_store_capacity_failure_maps_to_internal_error();
    test_department_and_auth_requests_do_not_pollute_each_other();
    test_unknown_request_behavior_is_unchanged();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d department core test(s) failed\n", failures);
        return 1;
    }

    puts("department core tests passed");
    return 0;
}
