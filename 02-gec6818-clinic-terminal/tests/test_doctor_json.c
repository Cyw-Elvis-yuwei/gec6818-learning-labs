#include "clinic_json.h"
#include "clinic_protocol.h"

#include <cjson/cJSON.h>

#include <stdio.h>
#include <string.h>

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

static void test_list_doctors_request_is_decoded(void)
{
    static const char json[] =
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":1}";
    ClinicRequest request;

    memset(&request, 0xA5, sizeof(request));
    CHECK(clinic_json_decode_request(
              json,
              sizeof(json) - 1U,
              &request) == CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_LIST_DOCTORS);
    CHECK(request.request_id == 501U);
    CHECK(request.department_id == 1);
    CHECK(request.username[0] == '\0');
    CHECK(request.password[0] == '\0');
}

static void test_department_id_requires_one_positive_int64_literal(void)
{
    static const char maximum_json[] =
        "{\"type\":\"list_doctors\","
        "\"request_id\":18446744073709551615,"
        "\"department_id\":9223372036854775807}";
    static const char *invalid_requests[] = {
        "{\"type\":\"list_doctors\",\"request_id\":501}",
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":0}",
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":-1}",
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":1.5}",
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":1e2}",
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":true}",
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":\"1\"}",
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":9223372036854775808}",
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":1,\"department_id\":2}",
        "{\"type\":\"list_doctors\",\"request_id\":501,\"payload\":{\"department_id\":1}}"};
    static const char trailing_json[] =
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":1} trailing";
    static const char embedded_null[] =
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":1}\0hidden";
    ClinicRequest request;
    size_t index;

    CHECK(clinic_json_decode_request(
              maximum_json,
              sizeof(maximum_json) - 1U,
              &request) == CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_LIST_DOCTORS);
    CHECK(request.request_id == UINT64_MAX);
    CHECK(request.department_id == INT64_MAX);

    for (index = 0U;
         index < sizeof(invalid_requests) / sizeof(invalid_requests[0]);
         ++index)
    {
        CHECK(clinic_json_decode_request(
                  invalid_requests[index],
                  strlen(invalid_requests[index]),
                  &request) == CLINIC_JSON_INVALID_REQUEST);
    }
    CHECK(clinic_json_decode_request(
              trailing_json,
              sizeof(trailing_json) - 1U,
              &request) == CLINIC_JSON_INVALID_JSON);
    CHECK(clinic_json_decode_request(
              embedded_null,
              sizeof(embedded_null) - 1U,
              &request) == CLINIC_JSON_INVALID_JSON);
}

static void test_doctor_success_response_contains_typed_array(void)
{
    ClinicResponse response;
    char output[2048] = {0};
    size_t output_length = 0U;
    cJSON *root;
    cJSON *doctors;
    cJSON *first;
    cJSON *second;
    cJSON *item;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_DOCTORS;
    response.request_id = 501U;
    response.doctor_count = 2U;
    response.doctors[0].id = 1;
    response.doctors[0].department_id = 1;
    snprintf(response.doctors[0].name, sizeof(response.doctors[0].name), "%s", "张医生");
    snprintf(response.doctors[0].title, sizeof(response.doctors[0].title), "%s", "主任医师");
    snprintf(response.doctors[0].specialty, sizeof(response.doctors[0].specialty), "%s", "心血管内科");
    response.doctors[1].id = 2;
    response.doctors[1].department_id = 1;
    snprintf(response.doctors[1].name, sizeof(response.doctors[1].name), "%s", "李医生");
    snprintf(response.doctors[1].title, sizeof(response.doctors[1].title), "%s", "副主任医师");
    snprintf(response.doctors[1].specialty, sizeof(response.doctors[1].specialty), "%s", "呼吸内科");
    snprintf(response.message, sizeof(response.message), "%s", "doctors retrieved");

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    if (output_length > 0U)
    {
        CHECK(memchr(output, '\n', output_length - 1U) == NULL);
    }

    root = cJSON_Parse(output);
    CHECK(root != NULL);
    CHECK(root == NULL ||
          cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    item = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL && item->valuedouble == 501.0);
    doctors = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "doctors");
    CHECK(cJSON_IsArray(doctors));
    CHECK(cJSON_GetArraySize(doctors) == 2);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    first = cJSON_IsArray(doctors) ? cJSON_GetArrayItem(doctors, 0) : NULL;
    item = cJSON_GetObjectItemCaseSensitive(first, "id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL && item->valuedouble == 1.0);
    item = cJSON_GetObjectItemCaseSensitive(first, "department_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL && item->valuedouble == 1.0);
    item = cJSON_GetObjectItemCaseSensitive(first, "name");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, "张医生") == 0);
    item = cJSON_GetObjectItemCaseSensitive(first, "title");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, "主任医师") == 0);
    item = cJSON_GetObjectItemCaseSensitive(first, "specialty");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, "心血管内科") == 0);
    second = cJSON_IsArray(doctors) ? cJSON_GetArrayItem(doctors, 1) : NULL;
    item = cJSON_GetObjectItemCaseSensitive(second, "id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL && item->valuedouble == 2.0);
    item = cJSON_GetObjectItemCaseSensitive(second, "name");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, "李医生") == 0);
    cJSON_Delete(root);
}

static void test_empty_doctors_and_maximum_request_id_are_exact(void)
{
    static const char exact_request_id[] =
        "\"request_id\":18446744073709551615";
    ClinicResponse response;
    char output[512] = {0};
    size_t output_length = 0U;
    cJSON *root;
    cJSON *doctors;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_DOCTORS;
    response.request_id = UINT64_MAX;
    snprintf(response.message, sizeof(response.message), "%s", "doctors retrieved");

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(strstr(output, exact_request_id) != NULL);
    CHECK(strstr(output, "18446744073709551616") == NULL);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    doctors = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "doctors");
    CHECK(cJSON_IsArray(doctors));
    CHECK(cJSON_GetArraySize(doctors) == 0);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    cJSON_Delete(root);
}

static void test_doctor_strings_are_utf8_and_json_escaped(void)
{
    static const char original_name[] = "张\"医\\生\n";
    static const char original_title[] = "主\t任医师";
    static const char original_specialty[] = "心血管\\内科\n门诊";
    ClinicResponse response;
    char output[1024] = {0};
    size_t output_length = 0U;
    cJSON *root;
    cJSON *doctors;
    cJSON *doctor;
    cJSON *item;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_DOCTORS;
    response.request_id = 502U;
    response.doctor_count = 1U;
    response.doctors[0].id = 7;
    response.doctors[0].department_id = 1;
    snprintf(response.doctors[0].name, sizeof(response.doctors[0].name), "%s", original_name);
    snprintf(response.doctors[0].title, sizeof(response.doctors[0].title), "%s", original_title);
    snprintf(response.doctors[0].specialty, sizeof(response.doctors[0].specialty), "%s", original_specialty);
    snprintf(response.message, sizeof(response.message), "%s", "doctors retrieved");

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(strstr(output, "\\\"") != NULL);
    CHECK(strstr(output, "\\\\") != NULL);
    CHECK(strstr(output, "\\n") != NULL);
    CHECK(strstr(output, "\\t") != NULL);
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');

    root = cJSON_Parse(output);
    CHECK(root != NULL);
    doctors = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "doctors");
    doctor = cJSON_IsArray(doctors) ? cJSON_GetArrayItem(doctors, 0) : NULL;
    item = cJSON_GetObjectItemCaseSensitive(doctor, "name");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, original_name) == 0);
    item = cJSON_GetObjectItemCaseSensitive(doctor, "title");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, original_title) == 0);
    item = cJSON_GetObjectItemCaseSensitive(doctor, "specialty");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, original_specialty) == 0);
    cJSON_Delete(root);
}

static void test_invalid_doctor_response_state_fails_without_leaking_data(void)
{
    ClinicResponse response;
    char output[512] = {0};
    char small_output[16];
    size_t output_length = 999U;
    cJSON *root;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_DOCTORS;
    response.request_id = 503U;
    response.doctor_count = CLINIC_MAX_DOCTORS + 1U;
    snprintf(response.message, sizeof(response.message), "%s", "doctors retrieved");
    memset(output, 'X', sizeof(output));
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_INVALID_ARGUMENT);
    CHECK(output_length == 0U);
    CHECK(output[0] == '\0');
    CHECK(output[1] == 'X');

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_DOCTORS;
    response.request_id = 504U;
    response.doctor_count = 1U;
    response.doctors[0].id = 1;
    response.doctors[0].department_id = 1;
    snprintf(response.doctors[0].name, sizeof(response.doctors[0].name), "%s", "张医生");
    snprintf(response.doctors[0].title, sizeof(response.doctors[0].title), "%s", "主任医师");
    snprintf(response.doctors[0].specialty, sizeof(response.doctors[0].specialty), "%s", "心血管内科");
    snprintf(response.message, sizeof(response.message), "%s", "doctors retrieved");
    memset(small_output, 'X', sizeof(small_output));
    output_length = 999U;
    CHECK(clinic_json_encode_response(
              &response,
              small_output,
              sizeof(small_output),
              &output_length) == CLINIC_JSON_OUTPUT_TOO_SMALL);
    CHECK(output_length == 0U);
    CHECK(small_output[0] == '\0');
    CHECK(small_output[1] == 'X');

    memset(response.doctors[0].name, 'A', sizeof(response.doctors[0].name));
    memset(output, 'X', sizeof(output));
    output_length = 999U;
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) != CLINIC_JSON_OK);
    CHECK(output_length == 0U);
    CHECK(output[0] == '\0');
    CHECK(output[1] == 'X');

    response.kind = CLINIC_RESPONSE_NONE;
    memset(output, 'X', sizeof(output));
    output_length = 999U;
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_INVALID_ARGUMENT);
    CHECK(output_length == 0U);
    CHECK(output[0] == '\0');
    CHECK(output[1] == 'X');

    memset(&response, 0, sizeof(response));
    response.request_id = 505U;
    response.doctor_count = 1U;
    response.doctors[0].id = 99;
    snprintf(response.doctors[0].name, sizeof(response.doctors[0].name), "%s", "stale");
    snprintf(response.error_code, sizeof(response.error_code), "%s", "DATABASE_ERROR");
    snprintf(response.message, sizeof(response.message), "%s", "database unavailable");
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    CHECK(strstr(output, "stale") == NULL);
    cJSON_Delete(root);
}

static void test_maximum_doctor_payload_is_encoded_without_silent_truncation(void)
{
    ClinicResponse response;
    char output[65536] = {0};
    size_t output_length = 0U;
    size_t index;
    cJSON *root;
    cJSON *doctors;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_DOCTORS;
    response.request_id = 506U;
    response.doctor_count = CLINIC_MAX_DOCTORS;
    snprintf(response.message, sizeof(response.message), "%s", "doctors retrieved");

    for (index = 0U; index < CLINIC_MAX_DOCTORS; ++index)
    {
        ClinicDoctor *doctor = &response.doctors[index];

        doctor->id = (int64_t)index + 1;
        doctor->department_id = 1;
        memset(doctor->name, 1, CLINIC_DOCTOR_NAME_MAX_LENGTH);
        doctor->name[CLINIC_DOCTOR_NAME_MAX_LENGTH] = '\0';
        memset(doctor->title, 1, CLINIC_DOCTOR_TITLE_MAX_LENGTH);
        doctor->title[CLINIC_DOCTOR_TITLE_MAX_LENGTH] = '\0';
        memset(doctor->specialty, 1, CLINIC_DOCTOR_SPECIALTY_MAX_LENGTH);
        doctor->specialty[CLINIC_DOCTOR_SPECIALTY_MAX_LENGTH] = '\0';
    }

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(output_length > CLINIC_MAX_FRAME_SIZE);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    doctors = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "doctors");
    CHECK(cJSON_IsArray(doctors));
    CHECK(cJSON_GetArraySize(doctors) == (int)CLINIC_MAX_DOCTORS);
    cJSON_Delete(root);
}

int main(void)
{
    test_list_doctors_request_is_decoded();
    test_department_id_requires_one_positive_int64_literal();
    test_doctor_success_response_contains_typed_array();
    test_empty_doctors_and_maximum_request_id_are_exact();
    test_doctor_strings_are_utf8_and_json_escaped();
    test_invalid_doctor_response_state_fails_without_leaking_data();
    test_maximum_doctor_payload_is_encoded_without_silent_truncation();

    if (failures != 0)
    {
        fprintf(stderr, "%d doctor JSON test(s) failed\n", failures);
        return 1;
    }

    puts("doctor JSON tests passed");
    return 0;
}
