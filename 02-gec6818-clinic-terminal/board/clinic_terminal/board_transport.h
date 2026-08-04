#ifndef CLINIC_TERMINAL_BOARD_TRANSPORT_H
#define CLINIC_TERMINAL_BOARD_TRANSPORT_H

#include <stddef.h>

typedef enum ClinicBoardTransportStatus {
    CLINIC_BOARD_TRANSPORT_OK = 0,
    CLINIC_BOARD_TRANSPORT_INVALID_ARGUMENT = -1,
    CLINIC_BOARD_TRANSPORT_INITIALIZATION_ERROR = -2,
    CLINIC_BOARD_TRANSPORT_SEND_ERROR = -3,
    CLINIC_BOARD_TRANSPORT_RECEIVE_ERROR = -4,
    CLINIC_BOARD_TRANSPORT_FRAME_ERROR = -5
} ClinicBoardTransportStatus;

ClinicBoardTransportStatus clinic_board_transport_exchange(
    const char *server_ip,
    const char *server_port,
    const char *request,
    size_t request_length,
    unsigned int timeout_ms,
    char *response,
    size_t response_capacity,
    size_t *response_length);

#endif
