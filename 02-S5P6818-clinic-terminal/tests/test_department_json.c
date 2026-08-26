#include "clinic_json.h"

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

static void test_list_departments_request_is_decoded(void)
{
    static const char json[] =
        "{\"type\":\"list_departments\",\"request_id\":201}";
    ClinicRequest request;

    memset(&request, 0xA5, sizeof(request));
    CHECK(clinic_json_decode_request(
              json,
              sizeof(json) - 1U,
              &request) == CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_LIST_DEPARTMENTS);
    CHECK(request.request_id == 201U);
    CHECK(request.username[0] == '\0');
    CHECK(request.password[0] == '\0');
}

static void test_department_success_response_contains_array(void)
{
    static const char *expected_names[] = {
        "内科", "外科", "儿科", "眼科", "口腔科"
    };
    ClinicResponse response;
    char output[1024];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *departments;
    size_t index;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_DEPARTMENTS;
    response.request_id = 201U;
    response.department_count = 5U;
    snprintf(response.message, sizeof(response.message), "%s", "departments listed");
    for (index = 0U; index < response.department_count; ++index)
    {
        response.departments[index].id = (int64_t)(index + 1U);
        snprintf(
            response.departments[index].name,
            sizeof(response.departments[index].name),
            "%s",
            expected_names[index]);
    }

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    departments = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "departments");
    CHECK(cJSON_IsArray(departments));
    CHECK(cJSON_GetArraySize(departments) == 5);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
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

static void test_department_request_id_boundaries(void)
{
    static const char zero_json[] =
        "{\"type\":\"list_departments\",\"request_id\":0}";
    static const char maximum_json[] =
        "{\"type\":\"list_departments\","
        "\"request_id\":18446744073709551615}";
    static const char *invalid_requests[] = {
        "{\"type\":\"list_departments\",\"request_id\":-1}",
        "{\"type\":\"list_departments\",\"request_id\":1.5}",
        "{\"type\":\"list_departments\",\"request_id\":1e2}",
        "{\"type\":\"list_departments\",\"request_id\":18446744073709551616}",
        "{\"type\":\"list_departments\",\"request_id\":true}",
        "{\"type\":\"list_departments\",\"request_id\":\"201\"}"
    };
    ClinicRequest request;
    size_t index;

    CHECK(clinic_json_decode_request(
              zero_json,
              sizeof(zero_json) - 1U,
              &request) == CLINIC_JSON_OK);
    CHECK(request.request_id == 0U);
    CHECK(clinic_json_decode_request(
              maximum_json,
              sizeof(maximum_json) - 1U,
              &request) == CLINIC_JSON_OK);
    CHECK(request.request_id == UINT64_MAX);

    for (index = 0U;
         index < sizeof(invalid_requests) / sizeof(invalid_requests[0]);
         ++index)
    {
        CHECK(clinic_json_decode_request(
                  invalid_requests[index],
                  strlen(invalid_requests[index]),
                  &request) == CLINIC_JSON_INVALID_REQUEST);
    }
}

static void test_department_request_structure_rules(void)
{
    static const char *invalid_requests[] = {
        "{\"type\":\"list_departments\"}",
        "{\"request_id\":201}",
        "{\"type\":\"list_departments\",\"type\":\"list_departments\",\"request_id\":201}",
        "{\"type\":\"list_departments\",\"request_id\":201,\"request_id\":202}",
        "{\"payload\":{\"type\":\"list_departments\",\"request_id\":201}}"
    };
    static const char *invalid_json[] = {
        "{not-json}",
        "{\"type\":\"list_departments\",\"request_id\":201} trailing"
    };
    static const char embedded_null[] =
        "{\"type\":\"list_departments\",\"request_id\":201}\0hidden";
    static const char extra_field[] =
        "{\"type\":\"list_departments\",\"request_id\":202,\"extra\":true}";
    ClinicRequest request;
    size_t index;

    for (index = 0U;
         index < sizeof(invalid_requests) / sizeof(invalid_requests[0]);
         ++index)
    {
        CHECK(clinic_json_decode_request(
                  invalid_requests[index],
                  strlen(invalid_requests[index]),
                  &request) == CLINIC_JSON_INVALID_REQUEST);
    }
    for (index = 0U;
         index < sizeof(invalid_json) / sizeof(invalid_json[0]);
         ++index)
    {
        CHECK(clinic_json_decode_request(
                  invalid_json[index],
                  strlen(invalid_json[index]),
                  &request) == CLINIC_JSON_INVALID_JSON);
    }
    CHECK(clinic_json_decode_request(
              embedded_null,
              sizeof(embedded_null) - 1U,
              &request) == CLINIC_JSON_INVALID_JSON);
    CHECK(clinic_json_decode_request(
              extra_field,
              sizeof(extra_field) - 1U,
              &request) == CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_LIST_DEPARTMENTS);
    CHECK(request.request_id == 202U);

    memset(&request, 0xA5, sizeof(request));
    CHECK(clinic_json_decode_request("{}", 2U, &request) ==
          CLINIC_JSON_INVALID_REQUEST);
    CHECK(request.type == 0);
    CHECK(request.request_id == 0U);
    CHECK(request.username[0] == '\0');
    CHECK(request.password[0] == '\0');
}

static void test_empty_department_response_uses_empty_array(void)
{
    ClinicResponse response;
    char output[512];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *departments;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_DEPARTMENTS;
    response.request_id = 203U;
    snprintf(response.message, sizeof(response.message), "%s", "departments listed");

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    CHECK(strchr(output, '\n') == output + output_length - 1U);

    root = cJSON_Parse(output);
    CHECK(root != NULL);
    departments = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "departments");
    CHECK(cJSON_IsArray(departments));
    CHECK(cJSON_GetArraySize(departments) == 0);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    cJSON_Delete(root);
}

static void test_department_count_over_capacity_fails_safely(void)
{
    ClinicResponse response;
    char output[64];
    size_t output_length = 999U;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_DEPARTMENTS;
    response.request_id = 204U;
    response.department_count = CLINIC_MAX_DEPARTMENTS + 1U;
    snprintf(response.message, sizeof(response.message), "%s", "departments listed");
    memset(output, 'X', sizeof(output));

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_INVALID_ARGUMENT);
    CHECK(output_length == 0U);
    CHECK(output[0] == '\0');
    CHECK(output[1] == 'X');
}

static void test_department_name_is_escaped_by_cjson(void)
{
    static const char original_name[] =
        "quote: \" slash: \\ newline:\n tab:\t";
    ClinicResponse response;
    char output[512];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *departments;
    cJSON *department;
    cJSON *name;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_DEPARTMENTS;
    response.request_id = 205U;
    response.department_count = 1U;
    response.departments[0].id = 1;
    snprintf(
        response.departments[0].name,
        sizeof(response.departments[0].name),
        "%s",
        original_name);
    snprintf(response.message, sizeof(response.message), "%s", "departments listed");

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(strstr(output, "\\\"") != NULL);
    CHECK(strstr(output, "\\\\") != NULL);
    CHECK(strstr(output, "\\n") != NULL);
    CHECK(strstr(output, "\\t") != NULL);
    CHECK(strchr(output, '\n') == output + output_length - 1U);

    root = cJSON_Parse(output);
    CHECK(root != NULL);
    departments = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "departments");
    department = cJSON_IsArray(departments)
        ? cJSON_GetArrayItem(departments, 0)
        : NULL;
    name = cJSON_GetObjectItemCaseSensitive(department, "name");
    CHECK(cJSON_IsString(name));
    CHECK(name != NULL && strcmp(name->valuestring, original_name) == 0);
    cJSON_Delete(root);
}

static void test_small_department_output_buffer_fails_without_truncation(void)
{
    ClinicResponse response;
    char output[16];
    size_t output_length = 999U;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_DEPARTMENTS;
    response.request_id = 206U;
    response.department_count = 1U;
    response.departments[0].id = 1;
    snprintf(response.departments[0].name, sizeof(response.departments[0].name), "%s", "内科");
    snprintf(response.message, sizeof(response.message), "%s", "departments listed");
    memset(output, 'X', sizeof(output));

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OUTPUT_TOO_SMALL);
    CHECK(output_length == 0U);
    CHECK(output[0] == '\0');
    CHECK(output[1] == 'X');
}

static void test_maximum_department_response_id_is_exact(void)
{
    static const char exact_request_id[] =
        "\"request_id\":18446744073709551615";
    ClinicResponse response;
    char output[512];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *departments;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_DEPARTMENTS;
    response.request_id = UINT64_MAX;
    response.department_count = 1U;
    response.departments[0].id = 1;
    snprintf(response.departments[0].name, sizeof(response.departments[0].name), "%s", "内科");
    snprintf(response.message, sizeof(response.message), "%s", "departments listed");

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(strstr(output, exact_request_id) != NULL);
    CHECK(strstr(output, "18446744073709551616") == NULL);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    departments = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "departments");
    CHECK(cJSON_IsArray(departments));
    CHECK(cJSON_GetArraySize(departments) == 1);
    cJSON_Delete(root);
}

static void test_failure_response_ignores_stale_departments(void)
{
    ClinicResponse response;
    char output[512];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *error_code;

    memset(&response, 0, sizeof(response));
    response.request_id = 207U;
    response.department_count = 5U;
    response.departments[0].id = 99;
    snprintf(response.departments[0].name, sizeof(response.departments[0].name), "%s", "stale");
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
          cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    error_code = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "error_code");
    CHECK(cJSON_IsString(error_code));
    CHECK(error_code != NULL &&
          strcmp(error_code->valuestring, "DATABASE_ERROR") == 0);
    cJSON_Delete(root);
}

static void test_untyped_success_is_not_assumed_to_be_departments(void)
{
    ClinicResponse response;
    char output[512];
    size_t output_length = 999U;
    ClinicJsonStatus status;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.request_id = 208U;
    response.user_id = 0;
    snprintf(response.message, sizeof(response.message), "%s", "future query success");
    memset(output, 'X', sizeof(output));

    status = clinic_json_encode_response(
        &response,
        output,
        sizeof(output),
        &output_length);
    CHECK(status == CLINIC_JSON_INVALID_ARGUMENT);
    if (status == CLINIC_JSON_OK)
    {
        cJSON *root = cJSON_Parse(output);

        CHECK(root != NULL);
        CHECK(root == NULL ||
              cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
        cJSON_Delete(root);
    }
    CHECK(output_length == 0U);
    CHECK(output[0] == '\0');
    CHECK(output[1] == 'X');

    response.kind = (ClinicResponseKind)999;
    output_length = 999U;
    memset(output, 'X', sizeof(output));
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_INVALID_ARGUMENT);
    CHECK(output_length == 0U);
    CHECK(output[0] == '\0');
    CHECK(output[1] == 'X');
}

static void test_auth_kind_with_zero_user_id_is_not_departments(void)
{
    ClinicResponse response;
    char output[512];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *user_id;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_AUTH;
    response.request_id = 209U;
    response.user_id = 0;
    snprintf(response.message, sizeof(response.message), "%s", "auth response");

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    root = cJSON_Parse(output);
    CHECK(root != NULL);
    user_id = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "user_id");
    CHECK(cJSON_IsNumber(user_id));
    CHECK(user_id != NULL && user_id->valuedouble == 0.0);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    cJSON_Delete(root);
}

int main(void)
{
    test_list_departments_request_is_decoded();
    test_department_success_response_contains_array();
    test_department_request_id_boundaries();
    test_department_request_structure_rules();
    test_empty_department_response_uses_empty_array();
    test_department_count_over_capacity_fails_safely();
    test_department_name_is_escaped_by_cjson();
    test_small_department_output_buffer_fails_without_truncation();
    test_maximum_department_response_id_is_exact();
    test_failure_response_ignores_stale_departments();
    test_untyped_success_is_not_assumed_to_be_departments();
    test_auth_kind_with_zero_user_id_is_not_departments();

    if (failures != 0)
    {
        fprintf(stderr, "%d department JSON test(s) failed\n", failures);
        return 1;
    }

    puts("department JSON tests passed");
    return 0;
}
