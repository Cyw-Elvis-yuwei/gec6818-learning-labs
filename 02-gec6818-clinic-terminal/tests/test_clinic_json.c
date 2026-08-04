#include "clinic_json.h"
#include "clinic_protocol.h"

#include <cjson/cJSON.h>

#include <stdint.h>
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

static void test_register_request_is_decoded(void)
{
    static const char json[] =
        "{\"type\":\"register\",\"request_id\":1,"
        "\"username\":\"chen\",\"password\":\"123456\"}";
    ClinicRequest request;

    memset(&request, 0xA5, sizeof(request));
    CHECK(clinic_json_decode_request(
              json,
              sizeof(json) - 1U,
              &request) == CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_REGISTER);
    CHECK(request.request_id == 1U);
    CHECK(strcmp(request.username, "chen") == 0);
    CHECK(strcmp(request.password, "123456") == 0);
}

static void test_login_request_is_decoded(void)
{
    static const char json[] =
        "{\"type\":\"login\",\"request_id\":2,"
        "\"username\":\"chen\",\"password\":\"123456\"}";
    ClinicRequest request;

    CHECK(clinic_json_decode_request(
              json,
              sizeof(json) - 1U,
              &request) == CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_LOGIN);
    CHECK(request.request_id == 2U);
    CHECK(strcmp(request.username, "chen") == 0);
    CHECK(strcmp(request.password, "123456") == 0);
}

static void test_request_id_boundaries(void)
{
    static const char zero_json[] =
        "{\"type\":\"login\",\"request_id\":0,"
        "\"username\":\"u\",\"password\":\"p\"}";
    static const char maximum_json[] =
        "{\"type\":\"login\",\"request_id\":18446744073709551615,"
        "\"username\":\"u\",\"password\":\"p\"}";
    ClinicRequest request;

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
}

static void test_invalid_request_ids_are_rejected(void)
{
    static const char *invalid_requests[] = {
        "{\"type\":\"login\",\"request_id\":-1,\"username\":\"u\",\"password\":\"p\"}",
        "{\"type\":\"login\",\"request_id\":1.5,\"username\":\"u\",\"password\":\"p\"}",
        "{\"type\":\"login\",\"request_id\":1e2,\"username\":\"u\",\"password\":\"p\"}",
        "{\"type\":\"login\",\"request_id\":18446744073709551616,\"username\":\"u\",\"password\":\"p\"}",
        "{\"type\":\"login\",\"request_id\":true,\"username\":\"u\",\"password\":\"p\"}"
    };
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
}

static void test_missing_and_wrong_type_fields_are_rejected(void)
{
    static const char *invalid_requests[] = {
        "{\"request_id\":1,\"username\":\"u\",\"password\":\"p\"}",
        "{\"type\":\"login\",\"username\":\"u\",\"password\":\"p\"}",
        "{\"type\":\"login\",\"request_id\":1,\"password\":\"p\"}",
        "{\"type\":\"login\",\"request_id\":1,\"username\":\"u\"}",
        "{\"type\":null,\"request_id\":1,\"username\":\"u\",\"password\":\"p\"}",
        "{\"type\":\"login\",\"request_id\":1,\"username\":[],\"password\":\"p\"}",
        "{\"type\":\"login\",\"request_id\":1,\"username\":\"u\",\"password\":{}}",
        "{\"type\":\"login\",\"request_id\":1,\"username\":\"u\",\"username\":\"v\",\"password\":\"p\"}"
    };
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
}

static void test_unknown_type_preserves_request_id(void)
{
    static const char json[] =
        "{\"type\":\"delete_user\",\"request_id\":77,"
        "\"username\":\"u\",\"password\":\"p\"}";
    static const char without_credentials[] =
        "{\"type\":\"unknown_operation\",\"request_id\":205}";
    ClinicRequest request;

    CHECK(clinic_json_decode_request(
              json,
              sizeof(json) - 1U,
              &request) == CLINIC_JSON_UNKNOWN_REQUEST);
    CHECK(request.request_id == 77U);

    CHECK(clinic_json_decode_request(
              without_credentials,
              sizeof(without_credentials) - 1U,
              &request) == CLINIC_JSON_UNKNOWN_REQUEST);
    CHECK(request.request_id == 205U);
}

static void test_invalid_json_inputs_are_rejected(void)
{
    static const char embedded_null[] =
        "{\"type\":\"login\",\"request_id\":8,"
        "\"username\":\"u\",\"password\":\"p\"}\0hidden";
    static const char *invalid_json[] = {
        "[]",
        "null",
        "{not-json}",
        "{\"type\":\"login\",\"request_id\":8,\"username\":\"u\",\"password\":\"p\"} trailing"
    };
    ClinicRequest request;
    size_t index;

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
    CHECK(clinic_json_decode_request(NULL, 0U, &request) ==
          CLINIC_JSON_INVALID_ARGUMENT);
    CHECK(clinic_json_decode_request("{}", 2U, NULL) ==
          CLINIC_JSON_INVALID_ARGUMENT);
}

static void test_nested_fields_cannot_replace_top_level_fields(void)
{
    static const char nested_only[] =
        "{\"payload\":{\"type\":\"login\",\"request_id\":5,"
        "\"username\":\"u\",\"password\":\"p\"}}";
    static const char nested_shadow[] =
        "{\"payload\":{\"type\":\"register\",\"request_id\":99,"
        "\"username\":\"shadow\",\"password\":\"shadow\"},"
        "\"type\":\"login\",\"request_id\":5,"
        "\"username\":\"top\",\"password\":\"secret\"}";
    ClinicRequest request;

    CHECK(clinic_json_decode_request(
              nested_only,
              sizeof(nested_only) - 1U,
              &request) == CLINIC_JSON_INVALID_REQUEST);
    CHECK(clinic_json_decode_request(
              nested_shadow,
              sizeof(nested_shadow) - 1U,
              &request) == CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_LOGIN);
    CHECK(request.request_id == 5U);
    CHECK(strcmp(request.username, "top") == 0);
}

static void test_credential_byte_boundaries_and_utf8(void)
{
    char username[CLINIC_USERNAME_MAX_LENGTH + 2U];
    char password[CLINIC_PASSWORD_MAX_LENGTH + 2U];
    char json[512];
    ClinicRequest request;
    int written;

    memset(username, 'u', CLINIC_USERNAME_MAX_LENGTH);
    username[CLINIC_USERNAME_MAX_LENGTH] = '\0';
    memset(password, 'p', CLINIC_PASSWORD_MAX_LENGTH);
    password[CLINIC_PASSWORD_MAX_LENGTH] = '\0';
    written = snprintf(
        json,
        sizeof(json),
        "{\"type\":\"register\",\"request_id\":9,"
        "\"username\":\"%s\",\"password\":\"%s\"}",
        username,
        password);
    CHECK(written > 0 && (size_t)written < sizeof(json));
    CHECK(clinic_json_decode_request(
              json,
              (size_t)written,
              &request) == CLINIC_JSON_OK);
    CHECK(strlen(request.username) == CLINIC_USERNAME_MAX_LENGTH);
    CHECK(strlen(request.password) == CLINIC_PASSWORD_MAX_LENGTH);

    username[CLINIC_USERNAME_MAX_LENGTH] = 'u';
    username[CLINIC_USERNAME_MAX_LENGTH + 1U] = '\0';
    written = snprintf(
        json,
        sizeof(json),
        "{\"type\":\"register\",\"request_id\":10,"
        "\"username\":\"%s\",\"password\":\"p\"}",
        username);
    CHECK(written > 0 && (size_t)written < sizeof(json));
    CHECK(clinic_json_decode_request(
              json,
              (size_t)written,
              &request) == CLINIC_JSON_INVALID_REQUEST);

    password[CLINIC_PASSWORD_MAX_LENGTH] = 'p';
    password[CLINIC_PASSWORD_MAX_LENGTH + 1U] = '\0';
    written = snprintf(
        json,
        sizeof(json),
        "{\"type\":\"register\",\"request_id\":11,"
        "\"username\":\"u\",\"password\":\"%s\"}",
        password);
    CHECK(written > 0 && (size_t)written < sizeof(json));
    CHECK(clinic_json_decode_request(
              json,
              (size_t)written,
              &request) == CLINIC_JSON_INVALID_REQUEST);

    written = snprintf(
        json,
        sizeof(json),
        "{\"type\":\"register\",\"request_id\":12,"
        "\"username\":\"陈晨\",\"password\":\"演示密码\"}");
    CHECK(written > 0 && (size_t)written < sizeof(json));
    CHECK(clinic_json_decode_request(
              json,
              (size_t)written,
              &request) == CLINIC_JSON_OK);
    CHECK(strcmp(request.username, "陈晨") == 0);
    CHECK(strcmp(request.password, "演示密码") == 0);
}

static void test_explicit_length_and_request_reset(void)
{
    static const char source[] =
        "{\"type\":\"login\",\"request_id\":13,"
        "\"username\":\"u\",\"password\":\"p\"}";
    static const char invalid[] = "{}";
    char without_terminator[sizeof(source) - 1U];
    ClinicRequest request;

    memcpy(without_terminator, source, sizeof(without_terminator));
    CHECK(clinic_json_decode_request(
              without_terminator,
              sizeof(without_terminator),
              &request) == CLINIC_JSON_OK);
    CHECK(request.request_id == 13U);

    CHECK(clinic_json_decode_request(
              invalid,
              sizeof(invalid) - 1U,
              &request) == CLINIC_JSON_INVALID_REQUEST);
    CHECK(request.type == 0);
    CHECK(request.request_id == 0U);
    CHECK(request.username[0] == '\0');
    CHECK(request.password[0] == '\0');
}

static void test_success_response_is_encoded(void)
{
    ClinicResponse response;
    char output[512];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *ok;
    cJSON *request_id;
    cJSON *user_id;
    cJSON *message;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_AUTH;
    response.request_id = 1U;
    response.user_id = 42;
    snprintf(response.message, sizeof(response.message), "%s", "register success");

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(output_length == strlen(output));
    CHECK(output_length > 0U && output[output_length - 1U] == '\n');
    CHECK(strchr(output, '\n') == output + output_length - 1U);
    CHECK(strstr(output, "password") == NULL);

    root = cJSON_Parse(output);
    CHECK(root != NULL);
    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    user_id = cJSON_GetObjectItemCaseSensitive(root, "user_id");
    message = cJSON_GetObjectItemCaseSensitive(root, "message");
    CHECK(cJSON_IsTrue(ok));
    CHECK(cJSON_IsNumber(request_id));
    CHECK(request_id->valuedouble == 1.0);
    CHECK(cJSON_IsNumber(user_id));
    CHECK(user_id->valuedouble == 42.0);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(cJSON_IsString(message));
    CHECK(strcmp(message->valuestring, "register success") == 0);
    cJSON_Delete(root);
}

static void test_uint64_max_response_id_is_exact(void)
{
    static const char exact_request_id[] =
        "\"request_id\":18446744073709551615";
    ClinicResponse response;
    char output[512];
    size_t output_length = 0U;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_AUTH;
    response.request_id = UINT64_MAX;
    response.user_id = 1;
    snprintf(response.message, sizeof(response.message), "%s", "login success");

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(strstr(output, exact_request_id) != NULL);
    CHECK(strstr(output, "18446744073709551616") == NULL);
    CHECK(output_length == strlen(output));
}

static void test_failure_response_is_encoded(void)
{
    ClinicResponse response;
    char output[512];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *ok;
    cJSON *request_id;
    cJSON *error_code;
    cJSON *message;

    memset(&response, 0, sizeof(response));
    response.request_id = 2U;
    snprintf(response.error_code, sizeof(response.error_code), "%s", "INVALID_PASSWORD");
    snprintf(
        response.message,
        sizeof(response.message),
        "%s",
        "username or password is incorrect");

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(output_length == strlen(output));
    CHECK(output[output_length - 1U] == '\n');
    CHECK(strchr(output, '\n') == output + output_length - 1U);
    CHECK(strstr(output, "password\"") == NULL);

    root = cJSON_Parse(output);
    CHECK(root != NULL);
    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    error_code = cJSON_GetObjectItemCaseSensitive(root, "error_code");
    message = cJSON_GetObjectItemCaseSensitive(root, "message");
    CHECK(cJSON_IsFalse(ok));
    CHECK(cJSON_IsNumber(request_id));
    CHECK(request_id->valuedouble == 2.0);
    CHECK(cJSON_IsString(error_code));
    CHECK(strcmp(error_code->valuestring, "INVALID_PASSWORD") == 0);
    CHECK(cJSON_IsString(message));
    CHECK(strcmp(message->valuestring, "username or password is incorrect") == 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    cJSON_Delete(root);
}

static void test_response_strings_are_escaped(void)
{
    static const char original_message[] =
        "quote: \" slash: \\ newline:\n tab:\t end";
    ClinicResponse response;
    char output[512];
    size_t output_length = 0U;
    cJSON *root;
    cJSON *message;

    memset(&response, 0, sizeof(response));
    response.request_id = 3U;
    snprintf(response.error_code, sizeof(response.error_code), "%s", "ESCAPE_TEST");
    snprintf(response.message, sizeof(response.message), "%s", original_message);

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(strchr(output, '\n') == output + output_length - 1U);
    CHECK(strstr(output, "\\\"") != NULL);
    CHECK(strstr(output, "\\\\") != NULL);
    CHECK(strstr(output, "\\n") != NULL);
    CHECK(strstr(output, "\\t") != NULL);

    root = cJSON_Parse(output);
    CHECK(root != NULL);
    message = cJSON_GetObjectItemCaseSensitive(root, "message");
    CHECK(cJSON_IsString(message));
    CHECK(strcmp(message->valuestring, original_message) == 0);
    cJSON_Delete(root);
}

static void test_small_output_buffer_fails_without_truncation(void)
{
    ClinicResponse response;
    char output[16];
    size_t output_length = 999U;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_AUTH;
    response.request_id = 4U;
    response.user_id = 1;
    snprintf(response.message, sizeof(response.message), "%s", "login success");
    memset(output, 'X', sizeof(output));

    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OUTPUT_TOO_SMALL);
    CHECK(output_length == 0U);
    CHECK(output[0] == '\0');
    CHECK(output[1] == 'X');

    CHECK(clinic_json_encode_response(NULL, output, sizeof(output), &output_length) ==
          CLINIC_JSON_INVALID_ARGUMENT);
    CHECK(clinic_json_encode_response(&response, NULL, 0U, &output_length) ==
          CLINIC_JSON_INVALID_ARGUMENT);
    CHECK(clinic_json_encode_response(&response, output, sizeof(output), NULL) ==
          CLINIC_JSON_INVALID_ARGUMENT);
}

static void test_admin_page_requests_are_strict(void)
{
    static const char VALID[] =
        "{\"type\":\"admin_list_users\",\"request_id\":81,"
        "\"after_id\":0,\"limit\":3}";
    static const char EXTRA[] =
        "{\"type\":\"admin_list_users\",\"request_id\":81,"
        "\"after_id\":0,\"limit\":3,\"password\":\"x\"}";
    static const char TOO_LARGE[] =
        "{\"type\":\"admin_list_tickets\",\"request_id\":82,"
        "\"after_id\":0,\"limit\":4}";
    ClinicRequest request;

    CHECK(clinic_json_decode_request(
              VALID,
              sizeof(VALID) - 1U,
              &request) == CLINIC_JSON_OK);
    CHECK(request.type == CLINIC_REQ_ADMIN_LIST_USERS);
    CHECK(request.after_id == 0);
    CHECK(request.limit == CLINIC_ADMIN_PAGE_MAX_ITEMS);
    CHECK(clinic_json_decode_request(
              EXTRA,
              sizeof(EXTRA) - 1U,
              &request) == CLINIC_JSON_INVALID_REQUEST);
    CHECK(clinic_json_decode_request(
              TOO_LARGE,
              sizeof(TOO_LARGE) - 1U,
              &request) == CLINIC_JSON_INVALID_REQUEST);
}

static void test_admin_responses_are_bounded_and_hide_passwords(void)
{
    ClinicResponse response;
    char output[CLINIC_MAX_FRAME_SIZE + 128U];
    size_t output_length = 0U;
    size_t index;
    size_t text_index;
    cJSON *root;
    cJSON *items;

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_ADMIN_USERS;
    response.request_id = 83U;
    response.admin_user_count = 1U;
    response.admin_users[0].id = 1;
    snprintf(
        response.admin_users[0].username,
        sizeof(response.admin_users[0].username),
        "%s",
        "demo-user");
    snprintf(response.message, sizeof(response.message), "%s", "users");
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(strstr(output, "password") == NULL);
    root = cJSON_Parse(output);
    items = cJSON_GetObjectItemCaseSensitive(root, "users");
    CHECK(cJSON_IsArray(items));
    CHECK(cJSON_GetArraySize(items) == 1);
    cJSON_Delete(root);

    memset(&response, 0, sizeof(response));
    response.ok = 1;
    response.kind = CLINIC_RESPONSE_ADMIN_TICKETS;
    response.request_id = 84U;
    response.admin_ticket_count = CLINIC_ADMIN_PAGE_MAX_ITEMS;
    response.has_more = 1;
    snprintf(response.message, sizeof(response.message), "%s", "tickets");
    for (index = 0U; index < response.admin_ticket_count; ++index)
    {
        ClinicAdminTicketRecord *record = &response.admin_tickets[index];

        record->ticket.id = (int64_t)index + 1;
        record->ticket.user_id = (int64_t)index + 1;
        record->ticket.department_id = 1;
        record->ticket.queue_number = (int64_t)index + 1;
        record->ticket.status = CLINIC_TICKET_WAITING;
        snprintf(
            record->ticket.service_date,
            sizeof(record->ticket.service_date),
            "%s",
            "2026-07-20");
        record->ticket.created_time = 1700000000;
        for (text_index = 0U;
             text_index < CLINIC_USERNAME_MAX_LENGTH;
             ++text_index)
        {
            record->username[text_index] = '\x01';
        }
        record->username[CLINIC_USERNAME_MAX_LENGTH] = '\0';
        for (text_index = 0U;
             text_index < CLINIC_DEPARTMENT_NAME_MAX_LENGTH;
             ++text_index)
        {
            record->department_name[text_index] = '\x02';
        }
        record->department_name[CLINIC_DEPARTMENT_NAME_MAX_LENGTH] = '\0';
    }
    CHECK(clinic_json_encode_response(
              &response,
              output,
              sizeof(output),
              &output_length) == CLINIC_JSON_OK);
    CHECK(output_length <= CLINIC_MAX_FRAME_SIZE);
    CHECK(strstr(output, "password") == NULL);
}

int main(void)
{
    test_register_request_is_decoded();
    test_login_request_is_decoded();
    test_request_id_boundaries();
    test_invalid_request_ids_are_rejected();
    test_missing_and_wrong_type_fields_are_rejected();
    test_unknown_type_preserves_request_id();
    test_invalid_json_inputs_are_rejected();
    test_nested_fields_cannot_replace_top_level_fields();
    test_credential_byte_boundaries_and_utf8();
    test_explicit_length_and_request_reset();
    test_success_response_is_encoded();
    test_uint64_max_response_id_is_exact();
    test_failure_response_is_encoded();
    test_response_strings_are_escaped();
    test_small_output_buffer_fails_without_truncation();
    test_admin_page_requests_are_strict();
    test_admin_responses_are_bounded_and_hide_passwords();

    if (failures != 0)
    {
        fprintf(stderr, "%d clinic JSON test(s) failed\n", failures);
        return 1;
    }

    puts("clinic JSON tests passed");
    return 0;
}
