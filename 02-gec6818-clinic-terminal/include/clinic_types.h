/*
 * 文件作用（答辩）：项目跨层共享的数据模型和业务枚举。
 * ClinicRequest/ClinicResponse 是 Handler 与 Core 的结构化边界；ClinicDepartment、
 * ClinicDoctor、ClinicTicket、ClinicQueueSummary 描述服务器和客户端共同理解的数据。
 * 固定容量和状态枚举集中定义，避免各模块自行猜测字段长度或号单状态。
 */
#ifndef CLINIC_TYPES_H
#define CLINIC_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define CLINIC_USERNAME_MAX_LENGTH 64U
#define CLINIC_PASSWORD_MAX_LENGTH 128U
#define CLINIC_ERROR_CODE_MAX_LENGTH 31U
#define CLINIC_MESSAGE_MAX_LENGTH 127U
#define CLINIC_DEPARTMENT_NAME_MAX_LENGTH 64U
#define CLINIC_MAX_DEPARTMENTS 32U
#define CLINIC_DOCTOR_NAME_MAX_LENGTH 64U
#define CLINIC_DOCTOR_TITLE_MAX_LENGTH 64U
#define CLINIC_DOCTOR_SPECIALTY_MAX_LENGTH 128U
#define CLINIC_MAX_DOCTORS 32U
#define CLINIC_SERVICE_DATE_LENGTH 10U
#define CLINIC_ADMIN_PAGE_MAX_ITEMS 3U

/* 科室是取号和医生筛选的业务单位；取号绑定 department_id，不绑定 doctor_id。 */
typedef struct ClinicDepartment
{
    int64_t id;
    char name[CLINIC_DEPARTMENT_NAME_MAX_LENGTH + 1U];
} ClinicDepartment;

/* 医生资料只用于查询展示，通过 department_id 归属科室，不代表指定医生预约。 */
typedef struct ClinicDoctor
{
    int64_t id;
    int64_t department_id;
    char name[CLINIC_DOCTOR_NAME_MAX_LENGTH + 1U];
    char title[CLINIC_DOCTOR_TITLE_MAX_LENGTH + 1U];
    char specialty[CLINIC_DOCTOR_SPECIALTY_MAX_LENGTH + 1U];
} ClinicDoctor;

/* 号单生命周期：等待 -> 已叫号；完成和取消状态为模型预留，均不计入前方等待。 */
typedef enum ClinicTicketStatus
{
    CLINIC_TICKET_WAITING = 0,
    CLINIC_TICKET_CALLED = 1,
    CLINIC_TICKET_COMPLETED = 2,
    CLINIC_TICKET_CANCELLED = 3
} ClinicTicketStatus;

/*
 * Ticket 是一张真实号单：id 是数据库主键，queue_number 是科室当天展示给用户的号码。
 * created_time/called_time 为 Unix 时间戳；尚未叫号时 called_time 使用 0。
 */
typedef struct ClinicTicket
{
    int64_t id;
    int64_t user_id;
    int64_t department_id;
    int64_t queue_number;
    ClinicTicketStatus status;
    char service_date[CLINIC_SERVICE_DATE_LENGTH + 1U];
    int64_t created_time;
    int64_t called_time;
} ClinicTicket;

/* 排队摘要由服务器计算；板端只展示，不根据本地列表自行推算。 */
typedef struct ClinicQueueSummary
{
    int64_t current_called_queue_number; /* 0 means no current call */
    int64_t waiting_ahead_count; /* non-negative */
} ClinicQueueSummary;

/* 管理台只读用户摘要：故意不包含 password。 */
typedef struct ClinicUserSummary
{
    int64_t id;
    char username[CLINIC_USERNAME_MAX_LENGTH + 1U];
} ClinicUserSummary;

/* 管理台号单视图包含 JOIN 后的用户名和科室名，便于直接核验。 */
typedef struct ClinicAdminTicketRecord
{
    ClinicTicket ticket;
    char username[CLINIC_USERNAME_MAX_LENGTH + 1U];
    char department_name[CLINIC_DEPARTMENT_NAME_MAX_LENGTH + 1U];
} ClinicAdminTicketRecord;

/* Handler 解码 JSON type 后转换成该枚举，Core 根据枚举分发业务。 */
typedef enum ClinicRequestType
{
    CLINIC_REQ_REGISTER = 1,
    CLINIC_REQ_LOGIN = 2,
    CLINIC_REQ_LIST_DEPARTMENTS = 3,
    CLINIC_REQ_LIST_DOCTORS = 4,
    CLINIC_REQ_CREATE_TICKET = 5,
    CLINIC_REQ_GET_TICKET = 6,
    CLINIC_REQ_CALL_NEXT = 7,
    CLINIC_REQ_GET_CURRENT_TICKET = 8,
    CLINIC_REQ_ADMIN_LIST_USERS = 9,
    CLINIC_REQ_ADMIN_LIST_TICKETS = 10
} ClinicRequestType;

typedef enum ClinicResponseKind
{
    CLINIC_RESPONSE_NONE = 0,
    CLINIC_RESPONSE_AUTH = 1,
    CLINIC_RESPONSE_DEPARTMENTS = 2,
    CLINIC_RESPONSE_DOCTORS = 3,
    CLINIC_RESPONSE_TICKET = 4,
    CLINIC_RESPONSE_ADMIN_USERS = 5,
    CLINIC_RESPONSE_ADMIN_TICKETS = 6
} ClinicResponseKind;

/*
 * 所有请求共用一个结构体，不同 type 只使用其中相关字段；未使用字段应保持为 0/空串。
 * request_id 用于让响应与请求对应，它不是用户 ID，也不是号单 ID。
 */
typedef struct ClinicRequest
{
    ClinicRequestType type;
    uint64_t request_id;
    char username[CLINIC_USERNAME_MAX_LENGTH + 1U];
    char password[CLINIC_PASSWORD_MAX_LENGTH + 1U];
    int64_t user_id;
    int64_t department_id;
    int64_t ticket_id;
    int64_t after_id;
    size_t limit;
} ClinicRequest;

/*
 * Core 生成的统一结果。ok 表示成功与否，kind 说明成功载荷是哪一种；
 * 失败时使用 error_code/message，成功时只读取 kind 对应的 user/数组/ticket 字段。
 */
typedef struct ClinicResponse
{
    int ok;
    ClinicResponseKind kind;
    uint64_t request_id;
    char error_code[CLINIC_ERROR_CODE_MAX_LENGTH + 1U];
    char message[CLINIC_MESSAGE_MAX_LENGTH + 1U];
    int64_t user_id;
    size_t department_count;
    ClinicDepartment departments[CLINIC_MAX_DEPARTMENTS];
    size_t doctor_count;
    ClinicDoctor doctors[CLINIC_MAX_DOCTORS];
    ClinicTicket ticket;
    int queue_summary_valid;
    ClinicQueueSummary queue_summary;
    size_t admin_user_count;
    ClinicUserSummary admin_users[CLINIC_ADMIN_PAGE_MAX_ITEMS];
    size_t admin_ticket_count;
    ClinicAdminTicketRecord admin_tickets[CLINIC_ADMIN_PAGE_MAX_ITEMS];
    int has_more;
} ClinicResponse;

#endif
