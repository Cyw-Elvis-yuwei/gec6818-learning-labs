#ifndef CLINIC_TERMINAL_DOCTOR_CLIENT_H
#define CLINIC_TERMINAL_DOCTOR_CLIENT_H

#include "clinic_types.h"

#include <stdint.h>

typedef enum ClinicDoctorListOutcome {
    CLINIC_DOCTOR_LIST_SUCCESS = 0,
    CLINIC_DOCTOR_LIST_NETWORK_ERROR,
    CLINIC_DOCTOR_LIST_PROTOCOL_ERROR,
    CLINIC_DOCTOR_LIST_SERVER_ERROR
} ClinicDoctorListOutcome;

typedef struct ClinicDoctorListResult {
    ClinicDoctorListOutcome outcome;
    size_t doctor_count;
    ClinicDoctor doctors[CLINIC_MAX_DOCTORS];
    char error_code[CLINIC_ERROR_CODE_MAX_LENGTH + 1U];
    char message[CLINIC_MESSAGE_MAX_LENGTH + 1U];
} ClinicDoctorListResult;

int clinic_doctor_list_request(
    const char *server_ip,
    const char *server_port,
    uint64_t request_id,
    int64_t department_id,
    unsigned int timeout_ms,
    ClinicDoctorListResult *result);

#endif
