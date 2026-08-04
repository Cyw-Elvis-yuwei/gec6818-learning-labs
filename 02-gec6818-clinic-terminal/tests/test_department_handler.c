#include "clinic_server_handler.h"
#include "clinic_store_sqlite.h"

#include <cjson/cJSON.h>

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/department_handler_test.db"

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
    size_t newline_count = 0U;
    const char *cursor;

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
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    CHECK(strstr(output, "\"password\":") == NULL);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    for (cursor = output; *cursor != '\0'; ++cursor)
    {
        if (*cursor == '\n')
        {
            ++newline_count;
        }
    }
    CHECK(newline_count == 1U);

    if (cJSON_IsArray(departments))
    {
        for (index = 0U; index < 5U; ++index)
        {
            cJSON *department = cJSON_GetArrayItem(departments, (int)index);
            cJSON *id = cJSON_GetObjectItemCaseSensitive(department, "id");
            cJSON *name = cJSON_GetObjectItemCaseSensitive(department, "name");

            CHECK(cJSON_IsNumber(id));
            CHECK(id != NULL && id->valuedouble == (double)(index + 1U));
            CHECK(cJSON_IsString(name));
            CHECK(name != NULL &&
                  strcmp(name->valuestring, expected_names[index]) == 0);
        }
    }
    cJSON_Delete(root);
}

static void check_error_response(
    const char *output,
    size_t output_length,
    uint64_t expected_request_id,
    const char *expected_error_code)
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
    CHECK(request_id != NULL &&
          request_id->valuedouble == (double)expected_request_id);
    CHECK(cJSON_IsString(error_code));
    CHECK(error_code != NULL &&
          strcmp(error_code->valuestring, expected_error_code) == 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    cJSON_Delete(root);
}

static void test_department_request_is_handled_end_to_end(void)
{
    static const char frame[] =
        "{\"type\":\"list_departments\",\"request_id\":201}";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[2048];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *ok;
    cJSON *request_id;
    cJSON *departments;

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
    ok = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "request_id");
    departments = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "departments");
    CHECK(cJSON_IsTrue(ok));
    CHECK(cJSON_IsNumber(request_id));
    CHECK(request_id != NULL && request_id->valuedouble == 201.0);
    CHECK(cJSON_IsArray(departments));
    CHECK(cJSON_GetArraySize(departments) == 5);
    cJSON_Delete(root);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_department_requests_repeat_and_preserve_uint64(void)
{
    static const char maximum_frame[] =
        "{\"type\":\"list_departments\","
        "\"request_id\":18446744073709551615}";
    static const char second_frame[] =
        "{\"type\":\"list_departments\",\"request_id\":202}";
    static const char exact_request_id[] =
        "\"request_id\":18446744073709551615";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[2048];
    size_t output_length = 0U;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_server_handler_init(&handler, &core) == 0);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              maximum_frame,
              sizeof(maximum_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    CHECK(strstr(output, exact_request_id) != NULL);
    check_department_response(output, output_length, UINT64_MAX);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              second_frame,
              sizeof(second_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_department_response(output, output_length, 202U);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_handler_state_isolated_across_department_and_auth_requests(void)
{
    static const char department_before[] =
        "{\"type\":\"list_departments\",\"request_id\":301}";
    static const char register_frame[] =
        "{\"type\":\"register\",\"request_id\":302,"
        "\"username\":\"department-handler-user\","
        "\"password\":\"teaching-password\"}";
    static const char department_after_register[] =
        "{\"type\":\"list_departments\",\"request_id\":303}";
    static const char wrong_login_frame[] =
        "{\"type\":\"login\",\"request_id\":304,"
        "\"username\":\"department-handler-user\","
        "\"password\":\"wrong-password\"}";
    static const char department_after_failure[] =
        "{\"type\":\"list_departments\",\"request_id\":305}";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[2048];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *ok;
    cJSON *request_id;
    cJSON *user_id;
    cJSON *error_code;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_server_handler_init(&handler, &core) == 0);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              department_before,
              sizeof(department_before) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_department_response(output, output_length, 301U);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              register_frame,
              sizeof(register_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    ok = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "request_id");
    user_id = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "user_id");
    CHECK(cJSON_IsTrue(ok));
    CHECK(cJSON_IsNumber(request_id));
    CHECK(request_id != NULL && request_id->valuedouble == 302.0);
    CHECK(cJSON_IsNumber(user_id));
    CHECK(user_id != NULL && user_id->valuedouble > 0.0);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    cJSON_Delete(root);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              department_after_register,
              sizeof(department_after_register) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_department_response(output, output_length, 303U);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              wrong_login_frame,
              sizeof(wrong_login_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    ok = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "request_id");
    error_code = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "error_code");
    CHECK(cJSON_IsFalse(ok));
    CHECK(cJSON_IsNumber(request_id));
    CHECK(request_id != NULL && request_id->valuedouble == 304.0);
    CHECK(cJSON_IsString(error_code));
    CHECK(error_code != NULL &&
          strcmp(error_code->valuestring, "INVALID_PASSWORD") == 0);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    cJSON_Delete(root);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              department_after_failure,
              sizeof(department_after_failure) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_department_response(output, output_length, 305U);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_handler_errors_and_recovery(void)
{
    static const char invalid_frame[] =
        "{\"type\":\"list_departments\",\"request_id\":306";
    static const char department_after_invalid[] =
        "{\"type\":\"list_departments\",\"request_id\":307}";
    static const char unknown_frame[] =
        "{\"type\":\"future_query\",\"request_id\":308,"
        "\"username\":\"u\",\"password\":\"p\"}";
    static const char department_after_unknown[] =
        "{\"type\":\"list_departments\",\"request_id\":309}";
    static const char department_closed[] =
        "{\"type\":\"list_departments\",\"request_id\":310}";
    static const char department_after_reopen[] =
        "{\"type\":\"list_departments\",\"request_id\":311}";
    static const char ping_frame[] =
        "{\"type\":\"ping\",\"request_id\":312}";
    static const char department_after_small[] =
        "{\"type\":\"list_departments\",\"request_id\":313}";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[2048];
    char small_output[16];
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
              invalid_frame,
              sizeof(invalid_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 0U, "INVALID_JSON");

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              department_after_invalid,
              sizeof(department_after_invalid) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_department_response(output, output_length, 307U);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              unknown_frame,
              sizeof(unknown_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 308U, "UNKNOWN_REQUEST");

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              department_after_unknown,
              sizeof(department_after_unknown) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_department_response(output, output_length, 309U);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              department_closed,
              sizeof(department_closed) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 310U, "DATABASE_ERROR");

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              department_after_reopen,
              sizeof(department_after_reopen) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_department_response(output, output_length, 311U);

    memset(small_output, 'X', sizeof(small_output));
    output_length = 999U;
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              department_after_small,
              sizeof(department_after_small) - 1U,
              small_output,
              sizeof(small_output),
              &output_length) == -1);
    CHECK(output_length == 0U);
    CHECK(small_output[0] == '\0');
    CHECK(small_output[1] == 'X');

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              department_after_small,
              sizeof(department_after_small) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_department_response(output, output_length, 313U);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              ping_frame,
              sizeof(ping_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    type = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "type");
    request_id = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsString(type));
    CHECK(type != NULL && strcmp(type->valuestring, "pong") == 0);
    CHECK(cJSON_IsNumber(request_id));
    CHECK(request_id != NULL && request_id->valuedouble == 312.0);
    cJSON_Delete(root);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

int main(void)
{
    (void)remove(TEST_DATABASE_PATH);
    test_department_request_is_handled_end_to_end();
    test_department_requests_repeat_and_preserve_uint64();
    test_handler_state_isolated_across_department_and_auth_requests();
    test_handler_errors_and_recovery();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d department handler test(s) failed\n", failures);
        return 1;
    }

    puts("department handler tests passed");
    return 0;
}
