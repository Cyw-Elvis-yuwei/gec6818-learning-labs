#ifndef CLINIC_TERMINAL_LOGIN_CLIENT_H
#define CLINIC_TERMINAL_LOGIN_CLIENT_H

#include "clinic_types.h"

#include <stdint.h>

typedef enum ClinicLoginOutcome {
    CLINIC_LOGIN_SUCCESS = 0,
    CLINIC_LOGIN_AUTH_FAILED,
    CLINIC_LOGIN_NETWORK_ERROR,
    CLINIC_LOGIN_PROTOCOL_ERROR
} ClinicLoginOutcome;

typedef struct ClinicLoginResult {
    ClinicLoginOutcome outcome;
    int64_t user_id;
    char error_code[CLINIC_ERROR_CODE_MAX_LENGTH + 1U];
    char message[CLINIC_MESSAGE_MAX_LENGTH + 1U];
} ClinicLoginResult;

typedef enum ClinicRegisterOutcome {
    CLINIC_REGISTER_SUCCESS = 0,
    CLINIC_REGISTER_REJECTED,
    CLINIC_REGISTER_NETWORK_ERROR,
    CLINIC_REGISTER_PROTOCOL_ERROR
} ClinicRegisterOutcome;

typedef struct ClinicRegisterResult {
    ClinicRegisterOutcome outcome;
    int64_t user_id;
    char error_code[CLINIC_ERROR_CODE_MAX_LENGTH + 1U];
    char message[CLINIC_MESSAGE_MAX_LENGTH + 1U];
} ClinicRegisterResult;

int clinic_login_request(
    const char *server_ip,
    const char *server_port,
    const char *username,
    const char *password,
    uint64_t request_id,
    unsigned int timeout_ms,
    ClinicLoginResult *result);

int clinic_register_request(
    const char *server_ip,
    const char *server_port,
    const char *username,
    const char *password,
    uint64_t request_id,
    unsigned int timeout_ms,
    ClinicRegisterResult *result);

#endif
