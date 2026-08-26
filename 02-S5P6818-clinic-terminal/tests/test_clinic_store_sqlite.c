#include "clinic_store_sqlite.h"

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/clinic_store_test.db"
#define INVALID_DATABASE_PATH "build/test/missing-directory/clinic.db"

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

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL)
    {
        return 0;
    }
    (void)fclose(file);
    return 1;
}

static void test_database_creation_and_user_round_trip(void)
{
    ClinicStore store;
    ClinicStoredUser user;
    int64_t user_id = 0;

    (void)remove(TEST_DATABASE_PATH);
    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(file_exists(TEST_DATABASE_PATH));

    CHECK(clinic_store_create_user(
              &store,
              "store-user",
              "teaching-demo-password",
              &user_id) == CLINIC_STORE_OK);
    CHECK(user_id > 0);

    memset(&user, 0, sizeof(user));
    CHECK(clinic_store_find_user_by_username(
              &store,
              "store-user",
              &user) == CLINIC_STORE_OK);
    CHECK(user.id == user_id);
    CHECK(strcmp(user.username, "store-user") == 0);
    CHECK(strcmp(user.password, "teaching-demo-password") == 0);
    CHECK(user.username[CLINIC_USERNAME_MAX_LENGTH] == '\0');
    CHECK(user.password[CLINIC_PASSWORD_MAX_LENGTH] == '\0');

    CHECK(clinic_store_find_user_by_username(
              &store,
              "missing-user",
              &user) == CLINIC_STORE_NOT_FOUND);
    CHECK(clinic_store_create_user(
              &store,
              "store-user",
              "different-password",
              &user_id) == CLINIC_STORE_DUPLICATE);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_find_user_by_username(
              &store,
              "store-user",
              &user) == CLINIC_STORE_INVALID_ARGUMENT);
}

static void test_repeated_open_and_close(void)
{
    unsigned int iteration;

    for (iteration = 0U; iteration < 10U; ++iteration)
    {
        ClinicStore store;
        ClinicStoredUser user;

        clinic_store_init(&store);
        CHECK(clinic_store_sqlite_open(
                  &store,
                  TEST_DATABASE_PATH) == CLINIC_STORE_OK);
        memset(&user, 0, sizeof(user));
        CHECK(clinic_store_find_user_by_username(
                  &store,
                  "store-user",
                  &user) == CLINIC_STORE_OK);
        CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    }
}

static void test_invalid_arguments_and_open_errors(void)
{
    ClinicStore store;
    ClinicStoredUser user;
    int64_t user_id = 0;

    clinic_store_init(NULL);
    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(NULL, TEST_DATABASE_PATH) ==
          CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_sqlite_open(&store, NULL) ==
          CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_sqlite_open(&store, "") ==
          CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_sqlite_open(&store, INVALID_DATABASE_PATH) ==
          CLINIC_STORE_DATABASE_ERROR);
    CHECK(store.operations == NULL);
    CHECK(store.context == NULL);

    CHECK(clinic_store_create_user(
              NULL,
              "user",
              "password",
              &user_id) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_create_user(
              &store,
              NULL,
              "password",
              &user_id) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_find_user_by_username(
              &store,
              "user",
              &user) == CLINIC_STORE_INVALID_ARGUMENT);
}

int main(void)
{
    (void)remove(TEST_DATABASE_PATH);
    test_database_creation_and_user_round_trip();
    test_repeated_open_and_close();
    test_invalid_arguments_and_open_errors();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d SQLite store test(s) failed\n", failures);
        return 1;
    }

    puts("SQLite store tests passed");
    return 0;
}
