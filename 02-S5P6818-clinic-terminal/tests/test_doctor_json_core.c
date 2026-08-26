#include "clinic_core.h"
#include "clinic_json.h"
#include "clinic_store_sqlite.h"

#include <cjson/cJSON.h>

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/doctor_json_core_test.db"

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

static ClinicJsonStatus execute_json_request(
    ClinicCore *core,
    const char *json,
    char *output,
    size_t output_capacity,
    size_t *output_length)
{
    ClinicRequest request;
    ClinicResponse response;
    ClinicJsonStatus status = clinic_json_decode_request(
        json,
        strlen(json),
        &request);

    if (status != CLINIC_JSON_OK)
    {
        return status;
    }
    if (clinic_core_handle(core, &request, &response) != 0)
    {
        return CLINIC_JSON_INVALID_ARGUMENT;
    }
    return clinic_json_encode_response(
        &response,
        output,
        output_capacity,
        output_length);
}

static void check_doctor_response(
    const char *output,
    size_t output_length,
    uint64_t expected_request_id,
    size_t expected_count,
    int64_t expected_first_id,
    int64_t expected_department_id)
{
    cJSON *root = cJSON_Parse(output);
    cJSON *doctors;
    cJSON *request_id;
    cJSON *first;
    cJSON *item;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return;
    }
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(request_id));
    if (expected_request_id != UINT64_MAX && cJSON_IsNumber(request_id))
    {
        CHECK(request_id->valuedouble == (double)expected_request_id);
    }
    doctors = cJSON_GetObjectItemCaseSensitive(root, "doctors");
    CHECK(cJSON_IsArray(doctors));
    CHECK(cJSON_GetArraySize(doctors) == (int)expected_count);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    CHECK(strstr(output, "\"password\"") == NULL);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    if (expected_count > 0U && cJSON_IsArray(doctors))
    {
        first = cJSON_GetArrayItem(doctors, 0);
        item = cJSON_GetObjectItemCaseSensitive(first, "id");
        CHECK(cJSON_IsNumber(item));
        CHECK(item != NULL && item->valuedouble == (double)expected_first_id);
        item = cJSON_GetObjectItemCaseSensitive(first, "department_id");
        CHECK(cJSON_IsNumber(item));
        CHECK(item != NULL && item->valuedouble == (double)expected_department_id);
        CHECK(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(first, "name")));
        CHECK(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(first, "title")));
        CHECK(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(first, "specialty")));
    }
    cJSON_Delete(root);
}

static void check_database_error_response(
    const char *output,
    uint64_t expected_request_id)
{
    cJSON *root = cJSON_Parse(output);
    cJSON *request_id;
    cJSON *error_code;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return;
    }
    CHECK(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(request_id));
    CHECK(request_id != NULL &&
          request_id->valuedouble == (double)expected_request_id);
    error_code = cJSON_GetObjectItemCaseSensitive(root, "error_code");
    CHECK(cJSON_IsString(error_code));
    CHECK(error_code != NULL &&
          strcmp(error_code->valuestring, "DATABASE_ERROR") == 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    cJSON_Delete(root);
}

static void test_doctor_json_core_round_trip_empty_and_uint64(void)
{
    static const char department_one[] =
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":1}";
    static const char department_two[] =
        "{\"type\":\"list_doctors\",\"request_id\":502,\"department_id\":2}";
    static const char unknown_department[] =
        "{\"type\":\"list_doctors\",\"request_id\":503,\"department_id\":999}";
    static const char maximum_request[] =
        "{\"type\":\"list_doctors\","
        "\"request_id\":18446744073709551615,\"department_id\":1}";
    ClinicStore store;
    ClinicCore core;
    char output[2048] = {0};
    size_t output_length = 0U;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    CHECK(execute_json_request(
              &core,
              department_one,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_doctor_response(output, output_length, 501U, 2U, 1, 1);

    CHECK(execute_json_request(
              &core,
              department_two,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_doctor_response(output, output_length, 502U, 1U, 3, 2);

    CHECK(execute_json_request(
              &core,
              unknown_department,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_doctor_response(output, output_length, 503U, 0U, 0, 999);

    CHECK(execute_json_request(
              &core,
              maximum_request,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_doctor_response(output, output_length, UINT64_MAX, 2U, 1, 1);
    CHECK(strstr(
              output,
              "\"request_id\":18446744073709551615") != NULL);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_interleaved_requests_and_database_recovery(void)
{
    static const char register_request[] =
        "{\"type\":\"register\",\"request_id\":601,"
        "\"username\":\"doctor-json-core-user\","
        "\"password\":\"teaching-password\"}";
    static const char doctor_request[] =
        "{\"type\":\"list_doctors\",\"request_id\":602,\"department_id\":1}";
    static const char department_request[] =
        "{\"type\":\"list_departments\",\"request_id\":603}";
    static const char login_request[] =
        "{\"type\":\"login\",\"request_id\":604,"
        "\"username\":\"doctor-json-core-user\","
        "\"password\":\"teaching-password\"}";
    static const char closed_request[] =
        "{\"type\":\"list_doctors\",\"request_id\":605,\"department_id\":1}";
    static const char reopened_request[] =
        "{\"type\":\"list_doctors\",\"request_id\":606,\"department_id\":2}";
    ClinicStore store;
    ClinicCore core;
    char output[2048] = {0};
    size_t output_length = 0U;
    cJSON *root;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    CHECK(execute_json_request(
              &core,
              register_request,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    CHECK(root == NULL ||
          cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    CHECK(root == NULL ||
          cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(root, "user_id")));
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    cJSON_Delete(root);

    CHECK(execute_json_request(
              &core,
              doctor_request,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_doctor_response(output, output_length, 602U, 2U, 1, 1);

    CHECK(execute_json_request(
              &core,
              department_request,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    CHECK(root == NULL ||
          cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root, "departments")));
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    cJSON_Delete(root);

    CHECK(execute_json_request(
              &core,
              login_request,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    CHECK(root == NULL ||
          cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    cJSON_Delete(root);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    CHECK(execute_json_request(
              &core,
              closed_request,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_database_error_response(output, 605U);

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(execute_json_request(
              &core,
              reopened_request,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_doctor_response(output, output_length, 606U, 1U, 3, 2);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

int main(void)
{
    (void)remove(TEST_DATABASE_PATH);
    test_doctor_json_core_round_trip_empty_and_uint64();
    test_interleaved_requests_and_database_recovery();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d doctor JSON/core test(s) failed\n", failures);
        return 1;
    }

    puts("doctor JSON/core tests passed");
    return 0;
}
