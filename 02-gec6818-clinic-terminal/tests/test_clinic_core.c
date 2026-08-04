#include "clinic_core.h"
#include "clinic_store_sqlite.h"

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/clinic_test.db"

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

static void test_user_can_register(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_REGISTER;
    request.request_id = 101U;
    snprintf(request.username, sizeof(request.username), "%s", "alice");
    snprintf(request.password, sizeof(request.password), "%s", "demo-password");

    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_AUTH);
    CHECK(response.request_id == request.request_id);
    CHECK(response.user_id > 0);
    CHECK(response.error_code[0] == '\0');

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_empty_credentials_are_rejected(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_REGISTER;
    request.request_id = 102U;
    snprintf(request.password, sizeof(request.password), "%s", "demo-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(strcmp(response.error_code, "INVALID_ARGUMENT") == 0);
    CHECK(response.request_id == request.request_id);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_REGISTER;
    request.request_id = 103U;
    snprintf(request.username, sizeof(request.username), "%s", "empty-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(strcmp(response.error_code, "INVALID_ARGUMENT") == 0);
    CHECK(response.request_id == request.request_id);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_credential_length_boundaries(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_REGISTER;
    request.request_id = 104U;
    memset(request.username, 'u', CLINIC_USERNAME_MAX_LENGTH);
    request.username[CLINIC_USERNAME_MAX_LENGTH] = '\0';
    memset(request.password, 'p', CLINIC_PASSWORD_MAX_LENGTH);
    request.password[CLINIC_PASSWORD_MAX_LENGTH] = '\0';
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_AUTH);
    CHECK(response.request_id == request.request_id);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_REGISTER;
    request.request_id = 105U;
    memset(request.username, 'x', sizeof(request.username));
    snprintf(request.password, sizeof(request.password), "%s", "demo-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(strcmp(response.error_code, "INVALID_ARGUMENT") == 0);
    CHECK(response.request_id == request.request_id);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_REGISTER;
    request.request_id = 106U;
    snprintf(request.username, sizeof(request.username), "%s", "long-password");
    memset(request.password, 'x', sizeof(request.password));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(strcmp(response.error_code, "INVALID_ARGUMENT") == 0);
    CHECK(response.request_id == request.request_id);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_duplicate_username_is_rejected(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_REGISTER;
    request.request_id = 107U;
    snprintf(request.username, sizeof(request.username), "%s", "alice");
    snprintf(request.password, sizeof(request.password), "%s", "another-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(strcmp(response.error_code, "USERNAME_EXISTS") == 0);
    CHECK(response.request_id == request.request_id);
    CHECK(response.user_id == 0);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_registered_user_can_login_after_reopen(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LOGIN;
    request.request_id = 108U;
    snprintf(request.username, sizeof(request.username), "%s", "alice");
    snprintf(request.password, sizeof(request.password), "%s", "demo-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 1);
    CHECK(response.kind == CLINIC_RESPONSE_AUTH);
    CHECK(response.request_id == request.request_id);
    CHECK(response.user_id > 0);
    CHECK(response.error_code[0] == '\0');

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_login_failures_are_distinct(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LOGIN;
    request.request_id = 109U;
    snprintf(request.username, sizeof(request.username), "%s", "missing-user");
    snprintf(request.password, sizeof(request.password), "%s", "demo-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(strcmp(response.error_code, "USER_NOT_FOUND") == 0);
    CHECK(response.request_id == request.request_id);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LOGIN;
    request.request_id = 110U;
    snprintf(request.username, sizeof(request.username), "%s", "alice");
    snprintf(request.password, sizeof(request.password), "%s", "wrong-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(strcmp(response.error_code, "INVALID_PASSWORD") == 0);
    CHECK(response.request_id == request.request_id);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_login_credentials_are_validated(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LOGIN;
    request.request_id = 111U;
    snprintf(request.password, sizeof(request.password), "%s", "demo-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(strcmp(response.error_code, "INVALID_ARGUMENT") == 0);
    CHECK(response.request_id == request.request_id);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LOGIN;
    request.request_id = 112U;
    snprintf(request.username, sizeof(request.username), "%s", "alice");
    memset(request.password, 'x', sizeof(request.password));
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(strcmp(response.error_code, "INVALID_ARGUMENT") == 0);
    CHECK(response.request_id == request.request_id);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_dispatch_and_null_arguments(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);

    memset(&request, 0, sizeof(request));
    request.type = (ClinicRequestType)999;
    request.request_id = 113U;
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(response.kind == CLINIC_RESPONSE_NONE);
    CHECK(strcmp(response.error_code, "UNKNOWN_REQUEST") == 0);
    CHECK(response.request_id == request.request_id);
    CHECK(response.error_code[CLINIC_ERROR_CODE_MAX_LENGTH] == '\0');
    CHECK(response.message[CLINIC_MESSAGE_MAX_LENGTH] == '\0');

    CHECK(clinic_core_handle(NULL, &request, &response) == -1);
    CHECK(strcmp(response.error_code, "INVALID_ARGUMENT") == 0);
    CHECK(response.request_id == request.request_id);
    CHECK(clinic_core_handle(&core, NULL, &response) == -1);
    CHECK(strcmp(response.error_code, "INVALID_ARGUMENT") == 0);
    CHECK(response.request_id == 0U);
    CHECK(clinic_core_handle(&core, &request, NULL) == -1);
    CHECK(clinic_core_init(NULL, &store) == -1);
    CHECK(clinic_core_init(&core, NULL) == -1);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_closed_database_returns_business_error(void)
{
    ClinicStore store;
    ClinicCore core;
    ClinicRequest request;
    ClinicResponse response;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_core_init(&core, &store) == 0);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);

    memset(&request, 0, sizeof(request));
    request.type = CLINIC_REQ_LOGIN;
    request.request_id = 114U;
    snprintf(request.username, sizeof(request.username), "%s", "alice");
    snprintf(request.password, sizeof(request.password), "%s", "demo-password");
    CHECK(clinic_core_handle(&core, &request, &response) == 0);
    CHECK(response.ok == 0);
    CHECK(response.kind == CLINIC_RESPONSE_NONE);
    CHECK(strcmp(response.error_code, "DATABASE_ERROR") == 0);
    CHECK(response.request_id == request.request_id);
}

int main(void)
{
    (void)remove(TEST_DATABASE_PATH);
    test_user_can_register();
    test_empty_credentials_are_rejected();
    test_credential_length_boundaries();
    test_duplicate_username_is_rejected();
    test_registered_user_can_login_after_reopen();
    test_login_failures_are_distinct();
    test_login_credentials_are_validated();
    test_dispatch_and_null_arguments();
    test_closed_database_returns_business_error();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d clinic core test(s) failed\n", failures);
        return 1;
    }

    puts("clinic core tests passed");
    return 0;
}
