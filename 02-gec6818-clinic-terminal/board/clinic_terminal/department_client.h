#ifndef CLINIC_TERMINAL_DEPARTMENT_CLIENT_H
#define CLINIC_TERMINAL_DEPARTMENT_CLIENT_H

#include "clinic_types.h"

#include <stdint.h>

typedef enum ClinicDepartmentListOutcome {
    CLINIC_DEPARTMENT_LIST_SUCCESS = 0,
    CLINIC_DEPARTMENT_LIST_NETWORK_ERROR,
    CLINIC_DEPARTMENT_LIST_PROTOCOL_ERROR,
    CLINIC_DEPARTMENT_LIST_SERVER_ERROR
} ClinicDepartmentListOutcome;

typedef struct ClinicDepartmentListResult {
    ClinicDepartmentListOutcome outcome;
    size_t department_count;
    ClinicDepartment departments[CLINIC_MAX_DEPARTMENTS];
    char error_code[CLINIC_ERROR_CODE_MAX_LENGTH + 1U];
    char message[CLINIC_MESSAGE_MAX_LENGTH + 1U];
} ClinicDepartmentListResult;

int clinic_department_list_request(
    const char *server_ip,
    const char *server_port,
    uint64_t request_id,
    unsigned int timeout_ms,
    ClinicDepartmentListResult *result);

#endif
