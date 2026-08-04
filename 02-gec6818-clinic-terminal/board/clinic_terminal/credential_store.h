#ifndef CLINIC_TERMINAL_CREDENTIAL_STORE_H
#define CLINIC_TERMINAL_CREDENTIAL_STORE_H

#include "clinic_types.h"

typedef enum ClinicCredentialStoreStatus {
    CLINIC_CREDENTIAL_STORE_OK = 0,
    CLINIC_CREDENTIAL_STORE_NOT_FOUND,
    CLINIC_CREDENTIAL_STORE_INVALID_ARGUMENT,
    CLINIC_CREDENTIAL_STORE_INVALID_DATA,
    CLINIC_CREDENTIAL_STORE_IO_ERROR
} ClinicCredentialStoreStatus;

typedef struct ClinicRememberedCredentials {
    char username[CLINIC_USERNAME_MAX_LENGTH + 1U];
    char password[CLINIC_PASSWORD_MAX_LENGTH + 1U];
} ClinicRememberedCredentials;

ClinicCredentialStoreStatus clinic_credential_store_load(
    const char *path,
    ClinicRememberedCredentials *credentials);

ClinicCredentialStoreStatus clinic_credential_store_save(
    const char *path,
    const char *username,
    const char *password);

ClinicCredentialStoreStatus clinic_credential_store_remove(
    const char *path);

#endif
