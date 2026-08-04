#include "clinic_core.h"
#include "clinic_json.h"
#include "clinic_store_sqlite.h"

#include <cjson/cJSON.h>

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/clinic_json_test.db"

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
    size_t *output_length,
    unsigned int *core_calls)
{
    ClinicRequest request;
    ClinicResponse response;
    ClinicJsonStatus status;

    if (output != NULL && output_capacity > 0U)
    {
        output[0] = '\0';
    }
    if (output_length != NULL)
    {
        *output_length = 0U;
    }

    status = clinic_json_decode_request(json, json_length, &request);
    if (status != CLINIC_JSON_OK)
    {
        return status;
    }

    ++(*core_calls);
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

static int64_t check_encoded_response(
    const char *output,
    int expected_ok,
    uint64_t expected_request_id,
    const char *expected_error_code)
{
    cJSON *root = cJSON_Parse(output);
    cJSON *ok;
    cJSON *request_id;
    cJSON *user_id;
    cJSON *error_code;
    int64_t parsed_user_id = 0;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return 0;
    }

    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    user_id = cJSON_GetObjectItemCaseSensitive(root, "user_id");
    error_code = cJSON_GetObjectItemCaseSensitive(root, "error_code");
    CHECK(expected_ok ? cJSON_IsTrue(ok) : cJSON_IsFalse(ok));
    CHECK(cJSON_IsNumber(request_id));
    if (cJSON_IsNumber(request_id))
    {
        CHECK(request_id->valuedouble == (double)expected_request_id);
    }
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    CHECK(strstr(output, "\"password\":") == NULL);

    if (expected_ok)
    {
        CHECK(cJSON_IsNumber(user_id));
        CHECK(error_code == NULL);
        if (cJSON_IsNumber(user_id))
        {
            parsed_user_id = (int64_t)user_id->valuedouble;
            CHECK(parsed_user_id > 0);
        }
    }
    else
    {
        CHECK(user_id == NULL);
        CHECK(cJSON_IsString(error_code));
        if (cJSON_IsString(error_code))
        {
            CHECK(strcmp(error_code->valuestring, expected_error_code) == 0);
        }
    }

    cJSON_Delete(root);
    return parsed_user_id;
}

static void test_register_json_reaches_core(void)
{
    static const char request_json[] =
        "{\"type\":\"register\",\"request_id\":201,"
        "\"username\":\"json-user\",\"password\":\"demo-password\"}";
    ClinicStore store;
    ClinicCore core;
    char output[512];
    size_t output_length = 0U;
    unsigned int core_calls = 0U;
    cJSON *root;
    cJSON *ok;
    cJSON *request_id;
    cJSON *user_id;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(execute_json_request(
              &core,
              request_json,
              sizeof(request_json) - 1U,
              output,
              sizeof(output),
              &output_length,
              &core_calls) == CLINIC_JSON_OK);
    CHECK(core_calls == 1U);
    CHECK(output_length == strlen(output));
    CHECK(strstr(output, "\"password\":") == NULL);

    root = cJSON_Parse(output);
    CHECK(root != NULL);
    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    user_id = cJSON_GetObjectItemCaseSensitive(root, "user_id");
    CHECK(cJSON_IsTrue(ok));
    CHECK(cJSON_IsNumber(request_id));
    CHECK(request_id->valuedouble == 201.0);
    CHECK(cJSON_IsNumber(user_id));
    CHECK(user_id->valuedouble > 0.0);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    cJSON_Delete(root);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_registration_and_login_business_results(void)
{
    static const char duplicate_json[] =
        "{\"type\":\"register\",\"request_id\":202,"
        "\"username\":\"json-user\",\"password\":\"other\"}";
    static const char login_json[] =
        "{\"type\":\"login\",\"request_id\":203,"
        "\"username\":\"json-user\",\"password\":\"demo-password\"}";
    static const char missing_user_json[] =
        "{\"type\":\"login\",\"request_id\":204,"
        "\"username\":\"missing-user\",\"password\":\"demo-password\"}";
    static const char wrong_password_json[] =
        "{\"type\":\"login\",\"request_id\":205,"
        "\"username\":\"json-user\",\"password\":\"wrong\"}";
    static const char login_again_json[] =
        "{\"type\":\"login\",\"request_id\":206,"
        "\"username\":\"json-user\",\"password\":\"demo-password\"}";
    ClinicStore store;
    ClinicCore core;
    char output[512];
    size_t output_length;
    unsigned int core_calls = 0U;
    int64_t first_login_user_id;
    int64_t second_login_user_id;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    CHECK(execute_json_request(
              &core,
              duplicate_json,
              sizeof(duplicate_json) - 1U,
              output,
              sizeof(output),
              &output_length,
              &core_calls) == CLINIC_JSON_OK);
    check_encoded_response(output, 0, 202U, "USERNAME_EXISTS");

    CHECK(execute_json_request(
              &core,
              login_json,
              sizeof(login_json) - 1U,
              output,
              sizeof(output),
              &output_length,
              &core_calls) == CLINIC_JSON_OK);
    first_login_user_id = check_encoded_response(output, 1, 203U, NULL);

    CHECK(execute_json_request(
              &core,
              missing_user_json,
              sizeof(missing_user_json) - 1U,
              output,
              sizeof(output),
              &output_length,
              &core_calls) == CLINIC_JSON_OK);
    check_encoded_response(output, 0, 204U, "USER_NOT_FOUND");

    CHECK(execute_json_request(
              &core,
              wrong_password_json,
              sizeof(wrong_password_json) - 1U,
              output,
              sizeof(output),
              &output_length,
              &core_calls) == CLINIC_JSON_OK);
    check_encoded_response(output, 0, 205U, "INVALID_PASSWORD");

    CHECK(execute_json_request(
              &core,
              login_again_json,
              sizeof(login_again_json) - 1U,
              output,
              sizeof(output),
              &output_length,
              &core_calls) == CLINIC_JSON_OK);
    second_login_user_id = check_encoded_response(output, 1, 206U, NULL);
    CHECK(first_login_user_id == second_login_user_id);
    CHECK(core_calls == 5U);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_invalid_json_does_not_enter_core(void)
{
    static const char invalid_json[] =
        "{\"type\":\"login\",\"request_id\":207,"
        "\"username\":\"json-user\",\"password\":}";
    ClinicStore store;
    ClinicCore core;
    char output[128];
    size_t output_length = 999U;
    unsigned int core_calls = 0U;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    memset(output, 'X', sizeof(output));

    CHECK(execute_json_request(
              &core,
              invalid_json,
              sizeof(invalid_json) - 1U,
              output,
              sizeof(output),
              &output_length,
              &core_calls) == CLINIC_JSON_INVALID_JSON);
    CHECK(core_calls == 0U);
    CHECK(output_length == 0U);
    CHECK(output[0] == '\0');
    CHECK(output[1] == 'X');
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_closed_database_error_is_encoded(void)
{
    static const char login_json[] =
        "{\"type\":\"login\",\"request_id\":208,"
        "\"username\":\"json-user\",\"password\":\"demo-password\"}";
    ClinicStore store;
    ClinicCore core;
    char output[512];
    size_t output_length = 0U;
    unsigned int core_calls = 0U;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);

    CHECK(execute_json_request(
              &core,
              login_json,
              sizeof(login_json) - 1U,
              output,
              sizeof(output),
              &output_length,
              &core_calls) == CLINIC_JSON_OK);
    CHECK(core_calls == 1U);
    check_encoded_response(output, 0, 208U, "DATABASE_ERROR");
}

int main(void)
{
    (void)remove(TEST_DATABASE_PATH);
    test_register_json_reaches_core();
    test_registration_and_login_business_results();
    test_invalid_json_does_not_enter_core();
    test_closed_database_error_is_encoded();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d clinic JSON/core test(s) failed\n", failures);
        return 1;
    }

    puts("clinic JSON/core tests passed");
    return 0;
}
