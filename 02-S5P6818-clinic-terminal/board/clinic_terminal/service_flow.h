#ifndef CLINIC_TERMINAL_SERVICE_FLOW_H
#define CLINIC_TERMINAL_SERVICE_FLOW_H

typedef enum ClinicServiceFlow {
    CLINIC_SERVICE_FLOW_DEPARTMENT_QUERY = 0,
    CLINIC_SERVICE_FLOW_DOCTOR_QUERY,
    CLINIC_SERVICE_FLOW_TICKET
} ClinicServiceFlow;

static inline int clinic_service_flow_is_valid(ClinicServiceFlow flow)
{
    return flow == CLINIC_SERVICE_FLOW_DEPARTMENT_QUERY ||
           flow == CLINIC_SERVICE_FLOW_DOCTOR_QUERY ||
           flow == CLINIC_SERVICE_FLOW_TICKET;
}

#endif
