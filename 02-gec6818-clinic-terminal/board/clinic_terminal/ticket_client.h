#ifndef CLINIC_TERMINAL_TICKET_CLIENT_H
#define CLINIC_TERMINAL_TICKET_CLIENT_H

#include "clinic_types.h"

#include <stdint.h>

typedef enum ClinicTicketCreateOutcome {
    CLINIC_TICKET_CREATE_SUCCESS = 0,
    CLINIC_TICKET_CREATE_EXISTING,
    CLINIC_TICKET_CREATE_NETWORK_ERROR,
    CLINIC_TICKET_CREATE_PROTOCOL_ERROR,
    CLINIC_TICKET_CREATE_SERVER_ERROR
} ClinicTicketCreateOutcome;

typedef struct ClinicTicketCreateResult {
    ClinicTicketCreateOutcome outcome;
    ClinicTicket ticket;
    char error_code[CLINIC_ERROR_CODE_MAX_LENGTH + 1U];
    char message[CLINIC_MESSAGE_MAX_LENGTH + 1U];
} ClinicTicketCreateResult;

typedef enum ClinicCurrentTicketOutcome {
    CLINIC_CURRENT_TICKET_SUCCESS = 0,
    CLINIC_CURRENT_TICKET_NO_TICKET,
    CLINIC_CURRENT_TICKET_NETWORK_ERROR,
    CLINIC_CURRENT_TICKET_PROTOCOL_ERROR,
    CLINIC_CURRENT_TICKET_SERVER_ERROR
} ClinicCurrentTicketOutcome;

typedef struct ClinicCurrentTicketResult {
    ClinicCurrentTicketOutcome outcome;
    ClinicTicket ticket;
    ClinicQueueSummary queue_summary;
    char error_code[CLINIC_ERROR_CODE_MAX_LENGTH + 1U];
    char message[CLINIC_MESSAGE_MAX_LENGTH + 1U];
} ClinicCurrentTicketResult;

int clinic_ticket_create_request(
    const char *server_ip,
    const char *server_port,
    uint64_t request_id,
    int64_t user_id,
    int64_t department_id,
    unsigned int timeout_ms,
    ClinicTicketCreateResult *result);

int clinic_ticket_get_current_request(
    const char *server_ip,
    const char *server_port,
    uint64_t request_id,
    int64_t user_id,
    unsigned int timeout_ms,
    ClinicCurrentTicketResult *result);

#endif
