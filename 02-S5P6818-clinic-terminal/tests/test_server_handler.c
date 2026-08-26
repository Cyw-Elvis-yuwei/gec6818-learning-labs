#include "clinic_server_handler.h"
#include "clinic_store_sqlite.h"

#include <cjson/cJSON.h>

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/server_handler_test.db"

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

static int64_t check_business_response(
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

static void test_ping_is_dispatched(void)
{
    static const char frame[] = "{\"type\":\"ping\",\"request_id\":1}";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[512];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *type;
    cJSON *request_id;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_server_handler_init(&handler, &core) == 0);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              sizeof(frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    CHECK(output_length == strlen(output));
    CHECK(output[output_length - 1U] == '\n');

    root = cJSON_Parse(output);
    CHECK(root != NULL);
    type = cJSON_GetObjectItemCaseSensitive(root, "type");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsString(type));
    CHECK(strcmp(type->valuestring, "pong") == 0);
    CHECK(cJSON_IsNumber(request_id));
    CHECK(request_id->valuedouble == 1.0);
    cJSON_Delete(root);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_register_is_dispatched(void)
{
    static const char frame[] =
        "{\"type\":\"register\",\"request_id\":101,"
        "\"username\":\"handler-user\",\"password\":\"123456\"}";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[512];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *ok;
    cJSON *request_id;
    cJSON *user_id;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_server_handler_init(&handler, &core) == 0);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              sizeof(frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);

    root = cJSON_Parse(output);
    CHECK(root != NULL);
    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    user_id = cJSON_GetObjectItemCaseSensitive(root, "user_id");
    CHECK(cJSON_IsTrue(ok));
    CHECK(cJSON_IsNumber(request_id));
    CHECK(request_id->valuedouble == 101.0);
    CHECK(cJSON_IsNumber(user_id));
    CHECK(user_id->valuedouble > 0.0);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    CHECK(strstr(output, "\"password\":") == NULL);
    cJSON_Delete(root);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_small_output_buffer_fails_safely(void)
{
    static const char frame[] = "{\"type\":\"ping\",\"request_id\":2}";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[8];
    size_t output_length = 999U;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_server_handler_init(&handler, &core) == 0);
    memset(output, 'X', sizeof(output));
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              sizeof(frame) - 1U,
              output,
              sizeof(output),
              &output_length) == -1);
    CHECK(output_length == 0U);
    CHECK(output[0] == '\0');
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_business_results_and_continuous_requests(void)
{
    static const char duplicate[] =
        "{\"type\":\"register\",\"request_id\":102,"
        "\"username\":\"handler-user\",\"password\":\"other\"}";
    static const char login[] =
        "{\"type\":\"login\",\"request_id\":103,"
        "\"username\":\"handler-user\",\"password\":\"123456\"}";
    static const char missing[] =
        "{\"type\":\"login\",\"request_id\":104,"
        "\"username\":\"missing\",\"password\":\"123456\"}";
    static const char wrong[] =
        "{\"type\":\"login\",\"request_id\":105,"
        "\"username\":\"handler-user\",\"password\":\"wrong\"}";
    static const char login_again[] =
        "{\"type\":\"login\",\"request_id\":106,"
        "\"username\":\"handler-user\",\"password\":\"123456\"}";
    const char *frames[] = {duplicate, login, missing, wrong, login_again};
    const uint64_t request_ids[] = {102U, 103U, 104U, 105U, 106U};
    const int expected_ok[] = {0, 1, 0, 0, 1};
    const char *error_codes[] = {
        "USERNAME_EXISTS", NULL, "USER_NOT_FOUND", "INVALID_PASSWORD", NULL
    };
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[512];
    size_t output_length;
    size_t index;
    int64_t first_login_user_id = 0;
    int64_t second_login_user_id = 0;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_server_handler_init(&handler, &core) == 0);

    for (index = 0U; index < sizeof(frames) / sizeof(frames[0]); ++index)
    {
        int64_t user_id;

        CHECK(clinic_server_handler_handle_frame(
                  &handler,
                  frames[index],
                  strlen(frames[index]),
                  output,
                  sizeof(output),
                  &output_length) == 0);
        CHECK(output_length == strlen(output));
        user_id = check_business_response(
            output,
            expected_ok[index],
            request_ids[index],
            error_codes[index]);
        if (request_ids[index] == 103U)
        {
            first_login_user_id = user_id;
        }
        else if (request_ids[index] == 106U)
        {
            second_login_user_id = user_id;
        }
    }
    CHECK(first_login_user_id == second_login_user_id);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_invalid_and_unknown_requests_return_json_errors(void)
{
    static const char invalid[] =
        "{\"type\":\"login\",\"request_id\":107,"
        "\"username\":\"handler-user\",\"password\":}";
    static const char unknown[] =
        "{\"type\":\"remove\",\"request_id\":108,"
        "\"username\":\"handler-user\",\"password\":\"123456\"}";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[512];
    size_t output_length;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_server_handler_init(&handler, &core) == 0);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              invalid,
              sizeof(invalid) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_business_response(output, 0, 0U, "INVALID_JSON");

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              unknown,
              sizeof(unknown) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_business_response(output, 0, 108U, "UNKNOWN_REQUEST");
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_uint64_max_response_is_exact(void)
{
    static const char frame[] =
        "{\"type\":\"login\",\"request_id\":18446744073709551615,"
        "\"username\":\"handler-user\",\"password\":\"123456\"}";
    static const char exact_request_id[] =
        "\"request_id\":18446744073709551615";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[512];
    size_t output_length;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_server_handler_init(&handler, &core) == 0);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              sizeof(frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    CHECK(strstr(output, exact_request_id) != NULL);
    CHECK(output_length == strlen(output));
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_closed_database_returns_stable_error(void)
{
    static const char frame[] =
        "{\"type\":\"login\",\"request_id\":109,"
        "\"username\":\"handler-user\",\"password\":\"123456\"}";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[512];
    size_t output_length;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_server_handler_init(&handler, &core) == 0);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              sizeof(frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_business_response(output, 0, 109U, "DATABASE_ERROR");
}

int main(void)
{
    (void)remove(TEST_DATABASE_PATH);
    test_ping_is_dispatched();
    test_register_is_dispatched();
    test_small_output_buffer_fails_safely();
    test_business_results_and_continuous_requests();
    test_invalid_and_unknown_requests_return_json_errors();
    test_uint64_max_response_is_exact();
    test_closed_database_returns_stable_error();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d server handler test(s) failed\n", failures);
        return 1;
    }

    puts("server handler tests passed");
    return 0;
}
