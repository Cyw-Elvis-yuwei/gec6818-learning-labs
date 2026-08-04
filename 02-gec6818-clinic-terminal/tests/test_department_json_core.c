#include "clinic_core.h"
#include "clinic_json.h"
#include "clinic_store_sqlite.h"

#include <cjson/cJSON.h>

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/department_json_core_test.db"

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
    size_t json_length,
    char *output,
    size_t output_capacity,
    size_t *output_length)
{
    ClinicRequest request;
    ClinicResponse response;
    ClinicJsonStatus status;

    status = clinic_json_decode_request(json, json_length, &request);
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

static void check_department_response(
    const char *output,
    size_t output_length,
    uint64_t expected_request_id)
{
    static const char *expected_names[] = {
        "内科", "外科", "儿科", "眼科", "口腔科"
    };
    cJSON *root = cJSON_Parse(output);
    cJSON *ok;
    cJSON *request_id;
    cJSON *departments;
    size_t index;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return;
    }
    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    departments = cJSON_GetObjectItemCaseSensitive(root, "departments");
    CHECK(cJSON_IsTrue(ok));
    CHECK(cJSON_IsNumber(request_id));
    if (expected_request_id != UINT64_MAX && cJSON_IsNumber(request_id))
    {
        CHECK(request_id->valuedouble == (double)expected_request_id);
    }
    CHECK(cJSON_IsArray(departments));
    CHECK(cJSON_GetArraySize(departments) == 5);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "error_code") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    CHECK(strstr(output, "\"password\":") == NULL);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    CHECK(strchr(output, '\n') == output + output_length - 1U);

    if (cJSON_IsArray(departments))
    {
        for (index = 0U; index < 5U; ++index)
        {
            cJSON *department = cJSON_GetArrayItem(departments, (int)index);
            cJSON *id = cJSON_GetObjectItemCaseSensitive(department, "id");
            cJSON *name = cJSON_GetObjectItemCaseSensitive(department, "name");

            CHECK(cJSON_IsNumber(id));
            CHECK(id->valuedouble == (double)(index + 1U));
            CHECK(cJSON_IsString(name));
            CHECK(strcmp(name->valuestring, expected_names[index]) == 0);
        }
    }
    cJSON_Delete(root);
}

static int64_t check_auth_success(
    const char *output,
    uint64_t expected_request_id)
{
    cJSON *root = cJSON_Parse(output);
    cJSON *ok;
    cJSON *request_id;
    cJSON *user_id;
    int64_t parsed_user_id = 0;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return 0;
    }
    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    user_id = cJSON_GetObjectItemCaseSensitive(root, "user_id");
    CHECK(cJSON_IsTrue(ok));
    CHECK(cJSON_IsNumber(request_id));
    CHECK(request_id->valuedouble == (double)expected_request_id);
    CHECK(cJSON_IsNumber(user_id));
    if (cJSON_IsNumber(user_id))
    {
        parsed_user_id = (int64_t)user_id->valuedouble;
        CHECK(parsed_user_id > 0);
    }
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    cJSON_Delete(root);
    return parsed_user_id;
}

static void check_database_error_response(
    const char *output,
    uint64_t expected_request_id)
{
    cJSON *root = cJSON_Parse(output);
    cJSON *ok;
    cJSON *request_id;
    cJSON *error_code;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return;
    }
    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    error_code = cJSON_GetObjectItemCaseSensitive(root, "error_code");
    CHECK(cJSON_IsFalse(ok));
    CHECK(cJSON_IsNumber(request_id));
    CHECK(request_id->valuedouble == (double)expected_request_id);
    CHECK(cJSON_IsString(error_code));
    CHECK(strcmp(error_code->valuestring, "DATABASE_ERROR") == 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    cJSON_Delete(root);
}

static void test_department_json_core_round_trip(void)
{
    static const char first_request[] =
        "{\"type\":\"list_departments\",\"request_id\":201}";
    static const char second_request[] =
        "{\"type\":\"list_departments\",\"request_id\":202}";
    static const char maximum_request[] =
        "{\"type\":\"list_departments\","
        "\"request_id\":18446744073709551615}";
    static const char exact_request_id[] =
        "\"request_id\":18446744073709551615";
    ClinicStore store;
    ClinicCore core;
    char output[2048];
    size_t output_length = 0U;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    CHECK(execute_json_request(
              &core,
              first_request,
              sizeof(first_request) - 1U,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_department_response(output, output_length, 201U);

    CHECK(execute_json_request(
              &core,
              second_request,
              sizeof(second_request) - 1U,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_department_response(output, output_length, 202U);

    CHECK(execute_json_request(
              &core,
              maximum_request,
              sizeof(maximum_request) - 1U,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_department_response(output, output_length, UINT64_MAX);
    CHECK(strstr(output, exact_request_id) != NULL);
    CHECK(strstr(output, "18446744073709551616") == NULL);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_department_and_auth_json_requests_can_be_interleaved(void)
{
    static const char department_before[] =
        "{\"type\":\"list_departments\",\"request_id\":301}";
    static const char register_request[] =
        "{\"type\":\"register\",\"request_id\":302,"
        "\"username\":\"department-json-user\","
        "\"password\":\"teaching-password\"}";
    static const char department_after[] =
        "{\"type\":\"list_departments\",\"request_id\":303}";
    static const char login_request[] =
        "{\"type\":\"login\",\"request_id\":304,"
        "\"username\":\"department-json-user\","
        "\"password\":\"teaching-password\"}";
    ClinicStore store;
    ClinicCore core;
    char output[2048];
    size_t output_length = 0U;
    int64_t registered_user_id;
    int64_t logged_in_user_id;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    CHECK(execute_json_request(
              &core,
              department_before,
              sizeof(department_before) - 1U,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_department_response(output, output_length, 301U);

    CHECK(execute_json_request(
              &core,
              register_request,
              sizeof(register_request) - 1U,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    registered_user_id = check_auth_success(output, 302U);

    CHECK(execute_json_request(
              &core,
              department_after,
              sizeof(department_after) - 1U,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_department_response(output, output_length, 303U);

    CHECK(execute_json_request(
              &core,
              login_request,
              sizeof(login_request) - 1U,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    logged_in_user_id = check_auth_success(output, 304U);
    CHECK(logged_in_user_id == registered_user_id);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_closed_and_reopened_database_results(void)
{
    static const char success_request[] =
        "{\"type\":\"list_departments\",\"request_id\":401}";
    static const char closed_request[] =
        "{\"type\":\"list_departments\",\"request_id\":402}";
    static const char reopened_request[] =
        "{\"type\":\"list_departments\",\"request_id\":403}";
    ClinicStore store;
    ClinicCore core;
    char output[2048];
    size_t output_length = 0U;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(execute_json_request(
              &core,
              success_request,
              sizeof(success_request) - 1U,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_department_response(output, output_length, 401U);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    CHECK(execute_json_request(
              &core,
              closed_request,
              sizeof(closed_request) - 1U,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_database_error_response(output, 402U);

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(execute_json_request(
              &core,
              reopened_request,
              sizeof(reopened_request) - 1U,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    check_department_response(output, output_length, 403U);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

int main(void)
{
    (void)remove(TEST_DATABASE_PATH);
    test_department_json_core_round_trip();
    test_department_and_auth_json_requests_can_be_interleaved();
    test_closed_and_reopened_database_results();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d department JSON/core test(s) failed\n", failures);
        return 1;
    }

    puts("department JSON/core tests passed");
    return 0;
}
