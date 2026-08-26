#ifdef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include "credential_store.h"

#include <stdio.h>
#include <string.h>

#define TEST_PATH "build/test/remembered_credentials_test.dat"

static int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if(!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #condition);                           \
            failures++;                                                       \
        }                                                                      \
    } while(0)

static void test_missing_save_load_overwrite_and_remove(void)
{
    ClinicRememberedCredentials credentials;

    (void)clinic_credential_store_remove(TEST_PATH);
    CHECK(clinic_credential_store_load(TEST_PATH, &credentials) ==
          CLINIC_CREDENTIAL_STORE_NOT_FOUND);

    CHECK(clinic_credential_store_save(TEST_PATH, "测试用户", "pass123") ==
          CLINIC_CREDENTIAL_STORE_OK);
    CHECK(clinic_credential_store_load(TEST_PATH, &credentials) ==
          CLINIC_CREDENTIAL_STORE_OK);
    CHECK(strcmp(credentials.username, "测试用户") == 0);
    CHECK(strcmp(credentials.password, "pass123") == 0);

    CHECK(clinic_credential_store_save(TEST_PATH, "second", "new-password") ==
          CLINIC_CREDENTIAL_STORE_OK);
    CHECK(clinic_credential_store_load(TEST_PATH, &credentials) ==
          CLINIC_CREDENTIAL_STORE_OK);
    CHECK(strcmp(credentials.username, "second") == 0);
    CHECK(strcmp(credentials.password, "new-password") == 0);

    CHECK(clinic_credential_store_remove(TEST_PATH) ==
          CLINIC_CREDENTIAL_STORE_OK);
    CHECK(clinic_credential_store_remove(TEST_PATH) ==
          CLINIC_CREDENTIAL_STORE_OK);
    CHECK(clinic_credential_store_load(TEST_PATH, &credentials) ==
          CLINIC_CREDENTIAL_STORE_NOT_FOUND);
}

static void test_invalid_inputs_are_rejected(void)
{
    ClinicRememberedCredentials credentials;

    CHECK(clinic_credential_store_save(NULL, "user", "password") ==
          CLINIC_CREDENTIAL_STORE_INVALID_ARGUMENT);
    CHECK(clinic_credential_store_save(TEST_PATH, "", "password") ==
          CLINIC_CREDENTIAL_STORE_INVALID_ARGUMENT);
    CHECK(clinic_credential_store_save(TEST_PATH, "user\nname", "password") ==
          CLINIC_CREDENTIAL_STORE_INVALID_ARGUMENT);
    CHECK(clinic_credential_store_save(TEST_PATH, "user", "pass\nword") ==
          CLINIC_CREDENTIAL_STORE_INVALID_ARGUMENT);
    CHECK(clinic_credential_store_load(NULL, &credentials) ==
          CLINIC_CREDENTIAL_STORE_INVALID_ARGUMENT);
    CHECK(clinic_credential_store_load(TEST_PATH, NULL) ==
          CLINIC_CREDENTIAL_STORE_INVALID_ARGUMENT);
}

static void test_corrupt_file_is_rejected(void)
{
    FILE *file = fopen(TEST_PATH, "wb");
    ClinicRememberedCredentials credentials;

    CHECK(file != NULL);
    if(file == NULL) {
        return;
    }
    CHECK(fputs("BAD-FORMAT\nuser\npassword\n", file) >= 0);
    CHECK(fclose(file) == 0);
    CHECK(clinic_credential_store_load(TEST_PATH, &credentials) ==
          CLINIC_CREDENTIAL_STORE_INVALID_DATA);
    CHECK(credentials.username[0] == '\0');
    CHECK(credentials.password[0] == '\0');
    CHECK(clinic_credential_store_remove(TEST_PATH) ==
          CLINIC_CREDENTIAL_STORE_OK);
}

int main(void)
{
    test_missing_save_load_overwrite_and_remove();
    test_invalid_inputs_are_rejected();
    test_corrupt_file_is_rejected();

    if(failures != 0) {
        fprintf(stderr, "%d credential store test(s) failed\n", failures);
        return 1;
    }
    printf("credential store tests passed\n");
    return 0;
}
