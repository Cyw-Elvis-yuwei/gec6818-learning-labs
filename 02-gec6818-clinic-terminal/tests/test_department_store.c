#include "clinic_store_sqlite.h"

#include <sqlite3.h>

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/department_store_test.db"

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

static void test_new_database_contains_seeded_departments(void)
{
    static const char *expected_names[] = {
        "内科", "外科", "儿科", "眼科", "口腔科"
    };
    ClinicStore store;
    ClinicDepartment departments[CLINIC_MAX_DEPARTMENTS];
    size_t count = 0U;
    size_t index;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_store_list_departments(
              &store,
              departments,
              CLINIC_MAX_DEPARTMENTS,
              &count) == CLINIC_STORE_OK);
    CHECK(count == 5U);

    for (index = 0U; index < count && index < 5U; ++index)
    {
        CHECK(departments[index].id == (int64_t)(index + 1U));
        CHECK(strcmp(departments[index].name, expected_names[index]) == 0);
    }
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_legacy_users_database_is_preserved_and_seed_is_idempotent(void)
{
    static const char legacy_schema[] =
        "CREATE TABLE users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT NOT NULL UNIQUE,"
        "password TEXT NOT NULL,"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "INSERT INTO users (username, password) VALUES "
        "('legacy-user', 'teaching-password');";
    sqlite3 *database = NULL;
    ClinicStore store;
    ClinicStoredUser user;
    ClinicDepartment departments[CLINIC_MAX_DEPARTMENTS];
    size_t count = 0U;

    (void)remove(TEST_DATABASE_PATH);
    CHECK(sqlite3_open(TEST_DATABASE_PATH, &database) == SQLITE_OK);
    CHECK(sqlite3_exec(database, legacy_schema, NULL, NULL, NULL) == SQLITE_OK);
    CHECK(sqlite3_close(database) == SQLITE_OK);

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    memset(&user, 0, sizeof(user));
    CHECK(clinic_store_find_user_by_username(
              &store,
              "legacy-user",
              &user) == CLINIC_STORE_OK);
    CHECK(strcmp(user.password, "teaching-password") == 0);
    CHECK(clinic_store_list_departments(
              &store,
              departments,
              CLINIC_MAX_DEPARTMENTS,
              &count) == CLINIC_STORE_OK);
    CHECK(count == 5U);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    count = 0U;
    CHECK(clinic_store_list_departments(
              &store,
              departments,
              CLINIC_MAX_DEPARTMENTS,
              &count) == CLINIC_STORE_OK);
    CHECK(count == 5U);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_insufficient_capacity_is_reported(void)
{
    ClinicStore store;
    ClinicDepartment departments[4];
    size_t count = 999U;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_store_list_departments(
              &store,
              departments,
              sizeof(departments) / sizeof(departments[0]),
              &count) == CLINIC_STORE_CAPACITY_EXCEEDED);
    CHECK(count == 0U);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void test_invalid_arguments_and_closed_database(void)
{
    ClinicStore store;
    ClinicDepartment departments[CLINIC_MAX_DEPARTMENTS];
    size_t count = 0U;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_store_list_departments(
              NULL,
              departments,
              CLINIC_MAX_DEPARTMENTS,
              &count) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_list_departments(
              &store,
              NULL,
              CLINIC_MAX_DEPARTMENTS,
              &count) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_list_departments(
              &store,
              departments,
              0U,
              &count) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_list_departments(
              &store,
              departments,
              CLINIC_MAX_DEPARTMENTS,
              NULL) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    CHECK(clinic_store_list_departments(
              &store,
              departments,
              CLINIC_MAX_DEPARTMENTS,
              &count) == CLINIC_STORE_DATABASE_ERROR);
}

int main(void)
{
    (void)remove(TEST_DATABASE_PATH);
    test_new_database_contains_seeded_departments();
    test_legacy_users_database_is_preserved_and_seed_is_idempotent();
    test_insufficient_capacity_is_reported();
    test_invalid_arguments_and_closed_database();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d department store test(s) failed\n", failures);
        return 1;
    }

    puts("department store tests passed");
    return 0;
}
