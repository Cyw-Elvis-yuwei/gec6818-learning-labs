#include "clinic_server_handler.h"
#include "clinic_json.h"
#include "clinic_protocol.h"
#include "clinic_store_sqlite.h"

#include <cjson/cJSON.h>

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/doctor_handler_test.db"
#define OVERSIZED_DOCTOR_TEXT_LENGTH 21U

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

static void check_doctor_response(
    const char *output,
    size_t output_length,
    uint64_t expected_request_id,
    size_t expected_count,
    int64_t expected_first_id,
    int64_t expected_department_id)
{
    cJSON *root = cJSON_Parse(output);
    cJSON *request_id;
    cJSON *doctors;
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

static void check_error_response(
    const char *output,
    size_t output_length,
    uint64_t expected_request_id,
    const char *expected_error_code)
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
          strcmp(error_code->valuestring, expected_error_code) == 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    cJSON_Delete(root);
}

static ClinicStoreStatus return_oversized_doctor_list(
    void *context,
    int64_t department_id,
    ClinicDoctor *doctors,
    size_t capacity,
    size_t *count)
{
    size_t index;

    (void)context;
    if (department_id <= 0 || doctors == NULL || count == NULL ||
        capacity < CLINIC_MAX_DOCTORS)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    for (index = 0U; index < CLINIC_MAX_DOCTORS; ++index)
    {
        doctors[index].id = (int64_t)index + 1;
        doctors[index].department_id = department_id;
        memset(doctors[index].name, 'A', OVERSIZED_DOCTOR_TEXT_LENGTH);
        doctors[index].name[OVERSIZED_DOCTOR_TEXT_LENGTH] = '\0';
        memset(doctors[index].title, 'B', OVERSIZED_DOCTOR_TEXT_LENGTH);
        doctors[index].title[OVERSIZED_DOCTOR_TEXT_LENGTH] = '\0';
        memset(doctors[index].specialty, 'C', OVERSIZED_DOCTOR_TEXT_LENGTH);
        doctors[index].specialty[OVERSIZED_DOCTOR_TEXT_LENGTH] = '\0';
    }
    *count = CLINIC_MAX_DOCTORS;
    return CLINIC_STORE_OK;
}

static void test_oversized_doctor_response_returns_stable_protocol_error(void)
{
    static const ClinicStoreOperations operations = {
        .list_doctors = return_oversized_doctor_list};
    static const char frame[] =
        "{\"type\":\"list_doctors\",\"request_id\":513,\"department_id\":1}";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    ClinicResponse encoded_response;
    char encoded_output[65536] = {0};
    size_t encoded_length = 0U;
    char output[65536] = {0};
    size_t output_length = 0U;

    memset(&encoded_response, 0, sizeof(encoded_response));
    encoded_response.ok = 1;
    encoded_response.kind = CLINIC_RESPONSE_DOCTORS;
    encoded_response.request_id = 513U;
    snprintf(
        encoded_response.message,
        sizeof(encoded_response.message),
        "%s",
        "doctors retrieved");
    CHECK(return_oversized_doctor_list(
              NULL,
              1,
              encoded_response.doctors,
              CLINIC_MAX_DOCTORS,
              &encoded_response.doctor_count) == CLINIC_STORE_OK);
    CHECK(clinic_json_encode_response(
              &encoded_response,
              encoded_output,
              sizeof(encoded_output),
              &encoded_length) == CLINIC_JSON_OK);
    CHECK(encoded_length > CLINIC_MAX_FRAME_SIZE);
    CHECK(encoded_length < CLINIC_MAX_FRAME_SIZE + 128U);

    clinic_store_init(&store);
    store.operations = &operations;
    store.context = &store;
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_server_handler_init(&handler, &core) == 0);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              frame,
              sizeof(frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 513U, "RESPONSE_TOO_LARGE");
}

static void test_doctor_request_is_handled_end_to_end(void)
{
    static const char frame[] =
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":1}";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[2048] = {0};
    size_t output_length = 0U;

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
    check_doctor_response(output, output_length, 501U, 2U, 1, 1);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_repeated_empty_and_maximum_requests_are_isolated(void)
{
    static const char maximum_frame[] =
        "{\"type\":\"list_doctors\","
        "\"request_id\":18446744073709551615,\"department_id\":1}";
    static const char second_frame[] =
        "{\"type\":\"list_doctors\",\"request_id\":502,\"department_id\":2}";
    static const char empty_frame[] =
        "{\"type\":\"list_doctors\",\"request_id\":503,\"department_id\":999}";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[2048] = {0};
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
    check_doctor_response(output, output_length, UINT64_MAX, 2U, 1, 1);
    CHECK(strstr(
              output,
              "\"request_id\":18446744073709551615") != NULL);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              second_frame,
              sizeof(second_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_doctor_response(output, output_length, 502U, 1U, 3, 2);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              empty_frame,
              sizeof(empty_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_doctor_response(output, output_length, 503U, 0U, 0, 999);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_handler_errors_recovery_and_existing_requests(void)
{
    static const char invalid_frame[] =
        "{\"type\":\"list_doctors\",\"request_id\":504,\"department_id\":1";
    static const char invalid_fields_frame[] =
        "{\"type\":\"list_doctors\",\"request_id\":505,\"department_id\":0}";
    static const char unknown_frame[] =
        "{\"type\":\"future_query\",\"request_id\":506}";
    static const char doctor_frame[] =
        "{\"type\":\"list_doctors\",\"request_id\":507,\"department_id\":1}";
    static const char closed_frame[] =
        "{\"type\":\"list_doctors\",\"request_id\":508,\"department_id\":1}";
    static const char ping_frame[] =
        "{\"type\":\"ping\",\"request_id\":509}";
    static const char register_frame[] =
        "{\"type\":\"register\",\"request_id\":510,"
        "\"username\":\"doctor-handler-user\","
        "\"password\":\"teaching-password\"}";
    static const char wrong_login_frame[] =
        "{\"type\":\"login\",\"request_id\":511,"
        "\"username\":\"doctor-handler-user\","
        "\"password\":\"wrong-password\"}";
    static const char department_frame[] =
        "{\"type\":\"list_departments\",\"request_id\":512}";
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    char output[2048] = {0};
    char small_output[16];
    char medium_output[256];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *item;
    char oversized_frame[CLINIC_MAX_FRAME_SIZE + 2U];
    size_t doctor_frame_length = sizeof(doctor_frame) - 1U;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_server_handler_init(&handler, &core) == 0);

    memcpy(oversized_frame, doctor_frame, doctor_frame_length);
    memset(
        oversized_frame + doctor_frame_length,
        ' ',
        sizeof(oversized_frame) - doctor_frame_length - 1U);
    oversized_frame[sizeof(oversized_frame) - 1U] = '\0';
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              oversized_frame,
              sizeof(oversized_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 0U, "MESSAGE_TOO_LARGE");

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
              invalid_fields_frame,
              sizeof(invalid_fields_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 505U, "INVALID_REQUEST");

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              unknown_frame,
              sizeof(unknown_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 506U, "UNKNOWN_REQUEST");

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              doctor_frame,
              sizeof(doctor_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_doctor_response(output, output_length, 507U, 2U, 1, 1);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              closed_frame,
              sizeof(closed_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 508U, "DATABASE_ERROR");

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(small_output, 'X', sizeof(small_output));
    output_length = 999U;
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              doctor_frame,
              sizeof(doctor_frame) - 1U,
              small_output,
              sizeof(small_output),
              &output_length) == -1);
    CHECK(output_length == 0U);
    CHECK(small_output[0] == '\0');
    CHECK(small_output[1] == 'X');

    memset(medium_output, 'X', sizeof(medium_output));
    output_length = 999U;
    CHECK(clinic_server_handler_handle_frame(
              &handler,
              doctor_frame,
              sizeof(doctor_frame) - 1U,
              medium_output,
              sizeof(medium_output),
              &output_length) == -1);
    CHECK(output_length == 0U);
    CHECK(medium_output[0] == '\0');
    CHECK(medium_output[1] == 'X');

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              doctor_frame,
              sizeof(doctor_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_doctor_response(output, output_length, 507U, 2U, 1, 1);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              ping_frame,
              sizeof(ping_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    item = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "type");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, "pong") == 0);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    cJSON_Delete(root);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              register_frame,
              sizeof(register_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    CHECK(root == NULL ||
          cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    CHECK(root == NULL ||
          cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(root, "user_id")));
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    cJSON_Delete(root);

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              wrong_login_frame,
              sizeof(wrong_login_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    check_error_response(output, output_length, 511U, "INVALID_PASSWORD");

    CHECK(clinic_server_handler_handle_frame(
              &handler,
              department_frame,
              sizeof(department_frame) - 1U,
              output,
              sizeof(output),
              &output_length) == 0);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    CHECK(root == NULL ||
          cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root, "departments")));
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    cJSON_Delete(root);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

int main(void)
{
    (void)remove(TEST_DATABASE_PATH);
    test_doctor_request_is_handled_end_to_end();
    test_repeated_empty_and_maximum_requests_are_isolated();
    test_handler_errors_recovery_and_existing_requests();
    test_oversized_doctor_response_returns_stable_protocol_error();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d doctor handler test(s) failed\n", failures);
        return 1;
    }

    puts("doctor handler tests passed");
    return 0;
}
