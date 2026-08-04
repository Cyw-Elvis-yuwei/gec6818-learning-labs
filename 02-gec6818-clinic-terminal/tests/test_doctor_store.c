#include "clinic_store_sqlite.h"

#include <stdio.h>
#include <string.h>

#define TEST_DATABASE_PATH "build/test/doctor_store_test.db"

static int failures = 0;

typedef struct ExpectedDoctor
{
    int64_t id;
    int64_t department_id;
    const char *name;
    const char *title;
    const char *specialty;
} ExpectedDoctor;

static const ExpectedDoctor EXPECTED_DOCTORS[] = {
    {1, 1, "张医生", "主任医师", "心血管内科"},
    {2, 1, "李医生", "副主任医师", "呼吸内科"},
    {3, 2, "王医生", "主任医师", "普通外科"},
    {4, 3, "赵医生", "主治医师", "儿科常见病"},
    {5, 4, "陈医生", "副主任医师", "眼科"},
    {6, 5, "刘医生", "主治医师", "口腔科"}};

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if (!(condition))                                                    \
        {                                                                    \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static void test_department_one_contains_two_seeded_doctors(void)
{
    ClinicStore store;
    ClinicDoctor doctors[CLINIC_MAX_DOCTORS];
    size_t count = 0U;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_store_list_doctors(
              &store,
              1,
              doctors,
              CLINIC_MAX_DOCTORS,
              &count) == CLINIC_STORE_OK);
    CHECK(count == 2U);
    if (count == 2U)
    {
        CHECK(doctors[0].id == 1);
        CHECK(doctors[0].department_id == 1);
        CHECK(strcmp(doctors[0].name, "张医生") == 0);
        CHECK(strcmp(doctors[0].title, "主任医师") == 0);
        CHECK(strcmp(doctors[0].specialty, "心血管内科") == 0);
        CHECK(doctors[1].id == 2);
        CHECK(doctors[1].department_id == 1);
        CHECK(strcmp(doctors[1].name, "李医生") == 0);
        CHECK(strcmp(doctors[1].title, "副主任医师") == 0);
        CHECK(strcmp(doctors[1].specialty, "呼吸内科") == 0);
    }
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

static void check_expected_doctor(
    const ClinicDoctor *actual,
    const ExpectedDoctor *expected)
{
    CHECK(actual->id == expected->id);
    CHECK(actual->department_id == expected->department_id);
    CHECK(strcmp(actual->name, expected->name) == 0);
    CHECK(strcmp(actual->title, expected->title) == 0);
    CHECK(strcmp(actual->specialty, expected->specialty) == 0);
}

static void test_all_seeds_are_idempotent_and_unknown_department_is_empty(void)
{
    ClinicStore store;
    ClinicDoctor doctors[CLINIC_MAX_DOCTORS];
    size_t count = 0U;
    size_t expected_index = 0U;
    int64_t department_id;
    unsigned int open_index;

    for (open_index = 0U; open_index < 2U; ++open_index)
    {
        clinic_store_init(&store);
        CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
        expected_index = 0U;
        for (department_id = 1; department_id <= 5; ++department_id)
        {
            size_t index;
            size_t expected_count = department_id == 1 ? 2U : 1U;

            memset(doctors, 0, sizeof(doctors));
            count = 999U;
            CHECK(clinic_store_list_doctors(
                      &store,
                      department_id,
                      doctors,
                      CLINIC_MAX_DOCTORS,
                      &count) == CLINIC_STORE_OK);
            CHECK(count == expected_count);
            for (index = 0U;
                 index < count && index < CLINIC_MAX_DOCTORS;
                 ++index)
            {
                CHECK(expected_index <
                      sizeof(EXPECTED_DOCTORS) / sizeof(EXPECTED_DOCTORS[0]));
                if (expected_index <
                    sizeof(EXPECTED_DOCTORS) / sizeof(EXPECTED_DOCTORS[0]))
                {
                    check_expected_doctor(
                        &doctors[index],
                        &EXPECTED_DOCTORS[expected_index]);
                }
                ++expected_index;
            }
        }
        CHECK(expected_index ==
              sizeof(EXPECTED_DOCTORS) / sizeof(EXPECTED_DOCTORS[0]));

        count = 999U;
        CHECK(clinic_store_list_doctors(
                  &store,
                  999,
                  doctors,
                  CLINIC_MAX_DOCTORS,
                  &count) == CLINIC_STORE_OK);
        CHECK(count == 0U);
        CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    }
}

static void test_capacity_arguments_and_closed_database_are_reported(void)
{
    ClinicStore store;
    ClinicDoctor doctors[CLINIC_MAX_DOCTORS];
    size_t count = 999U;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);

    CHECK(clinic_store_list_doctors(
              &store,
              1,
              doctors,
              1U,
              &count) == CLINIC_STORE_CAPACITY_EXCEEDED);
    CHECK(count == 0U);
    CHECK(clinic_store_list_doctors(
              NULL,
              1,
              doctors,
              CLINIC_MAX_DOCTORS,
              &count) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_list_doctors(
              &store,
              0,
              doctors,
              CLINIC_MAX_DOCTORS,
              &count) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_list_doctors(
              &store,
              -1,
              doctors,
              CLINIC_MAX_DOCTORS,
              &count) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_list_doctors(
              &store,
              1,
              NULL,
              CLINIC_MAX_DOCTORS,
              &count) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_list_doctors(
              &store,
              1,
              doctors,
              0U,
              &count) == CLINIC_STORE_INVALID_ARGUMENT);
    CHECK(clinic_store_list_doctors(
              &store,
              1,
              doctors,
              CLINIC_MAX_DOCTORS,
              NULL) == CLINIC_STORE_INVALID_ARGUMENT);

    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
    count = 999U;
    CHECK(clinic_store_list_doctors(
              &store,
              1,
              doctors,
              CLINIC_MAX_DOCTORS,
              &count) == CLINIC_STORE_DATABASE_ERROR);
    CHECK(count == 0U);
}

static void test_existing_users_and_departments_remain_available(void)
{
    ClinicStore store;
    ClinicStoredUser user;
    ClinicDepartment departments[CLINIC_MAX_DEPARTMENTS];
    size_t department_count = 0U;
    int64_t user_id = 0;

    clinic_store_init(&store);
    CHECK(clinic_store_sqlite_open(&store, TEST_DATABASE_PATH) == CLINIC_STORE_OK);
    CHECK(clinic_store_create_user(
              &store,
              "doctor-store-user",
              "teaching-password",
              &user_id) == CLINIC_STORE_OK);
    CHECK(user_id > 0);
    memset(&user, 0, sizeof(user));
    CHECK(clinic_store_find_user_by_username(
              &store,
              "doctor-store-user",
              &user) == CLINIC_STORE_OK);
    CHECK(user.id == user_id);
    CHECK(strcmp(user.password, "teaching-password") == 0);
    CHECK(clinic_store_list_departments(
              &store,
              departments,
              CLINIC_MAX_DEPARTMENTS,
              &department_count) == CLINIC_STORE_OK);
    CHECK(department_count == 5U);
    CHECK(departments[0].id == 1);
    CHECK(strcmp(departments[0].name, "内科") == 0);
    CHECK(departments[4].id == 5);
    CHECK(strcmp(departments[4].name, "口腔科") == 0);
    CHECK(clinic_store_close(&store) == CLINIC_STORE_OK);
}

int main(void)
{
    (void)remove(TEST_DATABASE_PATH);
    test_department_one_contains_two_seeded_doctors();
    test_all_seeds_are_idempotent_and_unknown_department_is_empty();
    test_capacity_arguments_and_closed_database_are_reported();
    test_existing_users_and_departments_remain_available();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d doctor store test(s) failed\n", failures);
        return 1;
    }

    puts("doctor store tests passed");
    return 0;
}
