#include "clinic_core.h"
#include "clinic_store_sqlite.h"

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/doctor_core_test.db"

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

static ClinicStoreStatus report_doctor_capacity_exceeded(
    void *context,
    int64_t department_id,
    ClinicDoctor *doctors,
    size_t capacity,
    size_t *count)
{
    (void)context;
    (void)department_id;
    (void)doctors;
    (void)capacity;
    *count = 0U;
    return CLINIC_STORE_CAPACITY_EXCEEDED;
}

static void test_doctors_can_be_listed_by_department(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DOCTORS;
    request.request_id = 501U;
    request.department_id = 1;

    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_DOCTORS);
    CHECK(response.request_id == 501U);
    CHECK(response.doctor_count == 2U);
    if (response.doctor_count == 2U)
    {
        CHECK(response.doctors[0].id == 1);
        CHECK(response.doctors[0].department_id == 1);
        CHECK(strcmp(response.doctors[0].name, "张医生") == 0);
        CHECK(strcmp(response.doctors[0].title, "主任医师") == 0);
        CHECK(strcmp(response.doctors[0].specialty, "心血管内科") == 0);
        CHECK(response.doctors[1].id == 2);
        CHECK(response.doctors[1].department_id == 1);
        CHECK(strcmp(response.doctors[1].name, "李医生") == 0);
        CHECK(strcmp(response.doctors[1].title, "副主任医师") == 0);
        CHECK(strcmp(response.doctors[1].specialty, "呼吸内科") == 0);
    }

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_maximum_request_id_and_empty_result_are_preserved(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DOCTORS;
    request.request_id = UINT64_MAX;
    request.department_id = 999;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(request.department_id == 999);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_DOCTORS);
    CHECK(response.request_id == UINT64_MAX);
    CHECK(response.doctor_count == 0U);
    CHECK(response.doctors[0].id == 0);
    CHECK(response.doctors[0].name[0] == '\0');

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_consecutive_invalid_and_closed_requests_have_no_stale_doctors(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DOCTORS;
    request.request_id = 502U;
    request.department_id = 1;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.doctor_count == 2U);
    CHECK(response.doctors[1].id == 2);

    request.request_id = 503U;
    request.department_id = 2;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_DOCTORS);
    CHECK(response.request_id == 503U);
    CHECK(response.doctor_count == 1U);
    CHECK(response.doctors[0].id == 3);
    CHECK(response.doctors[1].id == 0);
    CHECK(response.doctors[1].name[0] == '\0');

    request.request_id = 504U;
    request.department_id = 0;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(response.kind == CLINIC_RESPONSE_NONE);
    CHECK(response.request_id == 504U);
    CHECK(strcmp(response.error_code, "INVALID_ARGUMENT") == 0);
    CHECK(response.doctor_count == 0U);
    CHECK(response.doctors[0].id == 0);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    request.request_id = 505U;
    request.department_id = 1;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(response.kind == CLINIC_RESPONSE_NONE);
    CHECK(response.request_id == 505U);
    CHECK(strcmp(response.error_code, "DATABASE_ERROR") == 0);
    CHECK(response.doctor_count == 0U);
    CHECK(response.doctors[0].id == 0);
}

static void test_store_capacity_failure_maps_to_internal_error(void)
{
    static const ClinicStoreOperations operations = {
        .list_doctors = report_doctor_capacity_exceeded};
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    store.operations = &operations;
    store.context = &store;
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DOCTORS;
    request.request_id = 506U;
    request.department_id = 1;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(response.kind == CLINIC_RESPONSE_NONE);
    CHECK(response.request_id == 506U);
    CHECK(strcmp(response.error_code, "INTERNAL_ERROR") == 0);
    CHECK(response.doctor_count == 0U);
}

static void test_doctor_department_and_auth_requests_do_not_pollute_each_other(void)
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
    request.type = CLINIC_REQ_LIST_DOCTORS;
    request.request_id = 507U;
    request.department_id = 1;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.kind == CLINIC_RESPONSE_DOCTORS);
    CHECK(response.doctor_count == 2U);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_REGISTER;
    request.request_id = 508U;
    snprintf(request.username, sizeof(request.username), "%s", "doctor-core-user");
    snprintf(request.password, sizeof(request.password), "%s", "teaching-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_AUTH);
    CHECK(response.doctor_count == 0U);
    CHECK(response.doctors[0].id == 0);
    registered_user_id = response.user_id;
    CHECK(registered_user_id > 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DEPARTMENTS;
    request.request_id = 509U;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_DEPARTMENTS);
    CHECK(response.department_count == 5U);
    CHECK(response.doctor_count == 0U);
    CHECK(response.doctors[0].id == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LOGIN;
    request.request_id = 510U;
    snprintf(request.username, sizeof(request.username), "%s", "doctor-core-user");
    snprintf(request.password, sizeof(request.password), "%s", "teaching-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_AUTH);
    CHECK(response.user_id == registered_user_id);
    CHECK(response.doctor_count == 0U);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LIST_DOCTORS;
    request.request_id = 511U;
    request.department_id = 2;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_DOCTORS);
    CHECK(response.user_id == 0);
    CHECK(response.department_count == 0U);
    CHECK(response.doctor_count == 1U);
    CHECK(response.doctors[0].id == 3);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

int main(void)
{
    (void)remove(TEST_DATABASE_PATH);
    test_doctors_can_be_listed_by_department();
    test_maximum_request_id_and_empty_result_are_preserved();
    test_consecutive_invalid_and_closed_requests_have_no_stale_doctors();
    test_store_capacity_failure_maps_to_internal_error();
    test_doctor_department_and_auth_requests_do_not_pollute_each_other();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d doctor core test(s) failed\n", failures);
        return 1;
    }

    puts("doctor core tests passed");
    return 0;
}
