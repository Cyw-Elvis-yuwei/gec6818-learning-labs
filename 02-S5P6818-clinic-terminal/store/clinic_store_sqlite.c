/*
 * 文件作用：ClinicStore 的 SQLite 具体实现，是服务器唯一直接执行 SQL 的模块。
 * 启动时创建 users、departments、doctors、tickets 等表并写入基础科室/医生数据；运行时
 * 使用 prepared statement 和参数绑定完成注册、登录查询、医生查询、取号、排队统计，
 * 以及管理台用户/号单的只读游标分页；管理台接口不会返回 password 字段。
 *
 * 并发与一致性：create_ticket 和 call_next 使用 BEGIN IMMEDIATE 事务。取号先检查同用户、
 * 同科室、同日期是否已有 WAITING/CALLED 号单，再分配队列号；叫号按 queue_number ASC、
 * id ASC 选择最早 WAITING 号单，更新为 CALLED 并写入服务器时间，失败统一回滚。
 *
 * 阅读地图：INITIALIZE_SCHEMA_SQL 定义持久化结构；sqlite_create_ticket() 展示防重复
 * 取号事务；sqlite_get_current_ticket() 组合本人号单、当前叫号和前方人数；
 * sqlite_call_next() 展示叫号事务；clinic_store_sqlite_open() 完成数据库打开和函数表绑定。
 * prepared statement 的固定流程是 prepare -> bind -> step -> column -> finalize。
 */
#include "clinic_store_sqlite.h"

#include <sqlite3.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* SQLite 实现私有上下文；抽象 Store 只把它当作 void*，只有本文件知道里面是 sqlite3。 */
typedef struct ClinicStoreSqliteContext
{
    sqlite3 *database;
} ClinicStoreSqliteContext;

/*
 * 数据库结构与基础数据：
 * users 保存账号；departments/doctors 保存查询资料；tickets 保存排队事实。
 * tickets 的 UNIQUE(department_id,date,queue_number) 防止同科室同日号码重复；
 * 部分唯一索引只约束 WAITING/CALLED，防止一个用户在同科室同日持有两张有效号单。
 */
static const char INITIALIZE_SCHEMA_SQL[] =
    "BEGIN IMMEDIATE;"
    "CREATE TABLE IF NOT EXISTS users ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "username TEXT NOT NULL UNIQUE,"
    "password TEXT NOT NULL,"
    "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
    ");"
    "CREATE TABLE IF NOT EXISTS departments ("
    "id INTEGER PRIMARY KEY,"
    "name TEXT NOT NULL UNIQUE"
    ");"
    "INSERT OR IGNORE INTO departments (id, name) VALUES "
    "(1, '内科'),"
    "(2, '外科'),"
    "(3, '儿科'),"
    "(4, '眼科'),"
    "(5, '口腔科');"
    "CREATE TABLE IF NOT EXISTS doctors ("
    "id INTEGER PRIMARY KEY,"
    "department_id INTEGER NOT NULL,"
    "name TEXT NOT NULL,"
    "title TEXT NOT NULL,"
    "specialty TEXT NOT NULL"
    ");"
    "INSERT OR IGNORE INTO doctors "
    "(id, department_id, name, title, specialty) VALUES "
    "(1, 1, '张医生', '主任医师', '心血管内科'),"
    "(2, 1, '李医生', '副主任医师', '呼吸内科'),"
    "(3, 2, '王医生', '主任医师', '普通外科'),"
    "(4, 3, '赵医生', '主治医师', '儿科常见病'),"
    "(5, 4, '陈医生', '副主任医师', '眼科'),"
    "(6, 5, '刘医生', '主治医师', '口腔科');"
    "CREATE TABLE IF NOT EXISTS tickets ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "user_id INTEGER NOT NULL,"
    "department_id INTEGER NOT NULL,"
    "queue_number INTEGER NOT NULL CHECK(queue_number > 0),"
    "status INTEGER NOT NULL CHECK(status BETWEEN 0 AND 3),"
    "service_date TEXT NOT NULL CHECK(length(service_date) = 10),"
    "created_time INTEGER NOT NULL CHECK(created_time > 0),"
    "called_time INTEGER,"
    "FOREIGN KEY(user_id) REFERENCES users(id),"
    "FOREIGN KEY(department_id) REFERENCES departments(id),"
    "UNIQUE(department_id, service_date, queue_number)"
    ");"
    "CREATE UNIQUE INDEX IF NOT EXISTS tickets_one_active_per_user "
    "ON tickets(user_id, department_id, service_date) "
    "WHERE status IN (0, 1);"
    "COMMIT;";

/* 只用于无需返回数据的固定 SQL，例如 BEGIN、COMMIT、ROLLBACK 和建表脚本。 */
static int execute_sql(sqlite3 *database, const char *sql)
{
    return sqlite3_exec(database, sql, NULL, NULL, NULL);
}

/* 通用外键存在性检查：绑定一个 ID，查询有行表示对象存在，无行不等于数据库错误。 */
static ClinicStoreStatus query_id_exists(
    sqlite3 *database,
    const char *sql,
    int64_t id,
    int *exists)
{
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_DATABASE_ERROR;
    int result = sqlite3_prepare_v2(database, sql, -1, &statement, NULL);

    *exists = 0;
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(statement, 1, (sqlite3_int64)id);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_step(statement);
        if (result == SQLITE_ROW)
        {
            *exists = 1;
            status = CLINIC_STORE_OK;
        }
        else if (result == SQLITE_DONE)
        {
            status = CLINIC_STORE_OK;
        }
    }
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    return status;
}

/* 服务日期和时间戳统一从服务器 SQLite 时钟取得，避免不同开发板本地时间不一致。 */
static ClinicStoreStatus query_ticket_clock(
    sqlite3 *database,
    char *service_date,
    int64_t *created_time)
{
    static const char SQL[] =
        "SELECT strftime('%Y-%m-%d','now','localtime'), "
        "CAST(strftime('%s','now') AS INTEGER);";
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_DATABASE_ERROR;
    int result = sqlite3_prepare_v2(database, SQL, -1, &statement, NULL);

    if (result == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW)
    {
        const unsigned char *date = sqlite3_column_text(statement, 0);
        int date_length = sqlite3_column_bytes(statement, 0);
        int64_t timestamp = (int64_t)sqlite3_column_int64(statement, 1);

        if (date != NULL && date_length == (int)CLINIC_SERVICE_DATE_LENGTH &&
            timestamp > 0)
        {
            memcpy(service_date, date, CLINIC_SERVICE_DATE_LENGTH);
            service_date[CLINIC_SERVICE_DATE_LENGTH] = '\0';
            *created_time = timestamp;
            status = CLINIC_STORE_OK;
        }
    }
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    return status;
}

/*
 * 把 SELECT 当前行按固定列顺序复制成 ClinicTicket，同时验证日期、状态和正整数约束。
 * called_time 在 WAITING 时允许为 SQL NULL，结构体中用 0 表示“尚未叫号”。
 */
static ClinicStoreStatus copy_ticket_from_statement(
    sqlite3_stmt *statement,
    ClinicTicket *ticket)
{
    const unsigned char *date = sqlite3_column_text(statement, 5);
    int date_length = sqlite3_column_bytes(statement, 5);
    int status = sqlite3_column_int(statement, 4);

    if (date == NULL || date_length != (int)CLINIC_SERVICE_DATE_LENGTH ||
        status < CLINIC_TICKET_WAITING || status > CLINIC_TICKET_CANCELLED)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }
    ticket->id = (int64_t)sqlite3_column_int64(statement, 0);
    ticket->user_id = (int64_t)sqlite3_column_int64(statement, 1);
    ticket->department_id = (int64_t)sqlite3_column_int64(statement, 2);
    ticket->queue_number = (int64_t)sqlite3_column_int64(statement, 3);
    ticket->status = (ClinicTicketStatus)status;
    memcpy(ticket->service_date, date, CLINIC_SERVICE_DATE_LENGTH);
    ticket->service_date[CLINIC_SERVICE_DATE_LENGTH] = '\0';
    ticket->created_time = (int64_t)sqlite3_column_int64(statement, 6);
    ticket->called_time = sqlite3_column_type(statement, 7) == SQLITE_NULL
        ? 0
        : (int64_t)sqlite3_column_int64(statement, 7);
    if (ticket->id <= 0 || ticket->user_id <= 0 ||
        ticket->department_id <= 0 || ticket->queue_number <= 0 ||
        ticket->created_time <= 0)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }
    return CLINIC_STORE_OK;
}

static ClinicStoreStatus sqlite_get_ticket(
    void *context,
    int64_t ticket_id,
    ClinicTicket *ticket)
{
    static const char GET_TICKET_SQL[] =
        "SELECT id,user_id,department_id,queue_number,status,service_date,"
        "created_time,called_time FROM tickets WHERE id = ?;";
    ClinicStoreSqliteContext *sqlite_context = context;
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_DATABASE_ERROR;
    ClinicTicket result_ticket;
    int result;

    if (ticket != NULL)
    {
        memset(ticket, 0, sizeof(*ticket));
    }
    if (sqlite_context == NULL || sqlite_context->database == NULL ||
        ticket_id <= 0 || ticket == NULL)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    memset(&result_ticket, 0, sizeof(result_ticket));

    result = sqlite3_prepare_v2(
        sqlite_context->database,
        GET_TICKET_SQL,
        -1,
        &statement,
        NULL);
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(
            statement,
            1,
            (sqlite3_int64)ticket_id);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_step(statement);
        if (result == SQLITE_ROW)
        {
            status = copy_ticket_from_statement(statement, &result_ticket);
        }
        else if (result == SQLITE_DONE)
        {
            status = CLINIC_STORE_TICKET_NOT_FOUND;
        }
    }
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    if (status == CLINIC_STORE_OK)
    {
        *ticket = result_ticket;
    }
    return status;
}

/*
 * 当前叫号取同科室同日期最近一次 CALLED：called_time DESC，时间相同再按 id DESC。
 * 没有任何已叫号记录不是错误，queue_number 保持 0，由 JSON 编码成 null。
 */
static ClinicStoreStatus query_current_called_queue_number(
    sqlite3 *database,
    int64_t department_id,
    const char *service_date,
    int64_t *queue_number)
{
    static const char SQL[] =
        "SELECT queue_number FROM tickets "
        "WHERE department_id = ? AND service_date = ? "
        "AND status = ? AND called_time IS NOT NULL "
        "ORDER BY called_time DESC, id DESC LIMIT 1;";
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_DATABASE_ERROR;
    int result;

    if (queue_number != NULL)
    {
        *queue_number = 0;
    }
    if (database == NULL || department_id <= 0 || service_date == NULL ||
        queue_number == NULL)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }

    result = sqlite3_prepare_v2(database, SQL, -1, &statement, NULL);
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(
            statement,
            1,
            (sqlite3_int64)department_id);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_text(
            statement,
            2,
            service_date,
            -1,
            SQLITE_TRANSIENT);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int(statement, 3, CLINIC_TICKET_CALLED);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_step(statement);
        if (result == SQLITE_ROW)
        {
            *queue_number = (int64_t)sqlite3_column_int64(statement, 0);
            status = *queue_number > 0
                ? CLINIC_STORE_OK
                : CLINIC_STORE_DATABASE_ERROR;
        }
        else if (result == SQLITE_DONE)
        {
            status = CLINIC_STORE_OK;
        }
    }
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    return status;
}

/*
 * 前方人数只计数同科室、同日期、仍为 WAITING 且 queue_number 小于本人号码的号单。
 * 已经 CALLED/COMPLETED/CANCELLED 的号单不再占用“前方等待”数量。
 */
static ClinicStoreStatus query_waiting_ahead_count(
    sqlite3 *database,
    int64_t department_id,
    const char *service_date,
    int64_t queue_number,
    int64_t *waiting_ahead_count)
{
    static const char SQL[] =
        "SELECT COUNT(*) FROM tickets "
        "WHERE department_id = ? AND service_date = ? "
        "AND status = ? AND queue_number < ?;";
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_DATABASE_ERROR;
    int result;

    if (waiting_ahead_count != NULL)
    {
        *waiting_ahead_count = 0;
    }
    if (database == NULL || department_id <= 0 || service_date == NULL ||
        queue_number <= 0 || waiting_ahead_count == NULL)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }

    result = sqlite3_prepare_v2(database, SQL, -1, &statement, NULL);
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(
            statement,
            1,
            (sqlite3_int64)department_id);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_text(
            statement,
            2,
            service_date,
            -1,
            SQLITE_TRANSIENT);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int(statement, 3, CLINIC_TICKET_WAITING);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(
            statement,
            4,
            (sqlite3_int64)queue_number);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_step(statement);
        if (result == SQLITE_ROW)
        {
            *waiting_ahead_count =
                (int64_t)sqlite3_column_int64(statement, 0);
            status = *waiting_ahead_count >= 0
                ? CLINIC_STORE_OK
                : CLINIC_STORE_DATABASE_ERROR;
        }
    }
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    return status;
}

/*
 * 一次 Store 调用组合完整排队摘要：先取服务器当天日期，再取用户当天最新号单；
 * 然后查询该科室当前叫号。只有本人仍为 WAITING 才计算前方人数，否则固定为 0。
 * 所有临时结果先放在局部变量中，全部成功后才写给输出参数，避免半成功数据泄漏。
 */
static ClinicStoreStatus sqlite_get_current_ticket(
    void *context,
    int64_t user_id,
    ClinicTicket *ticket,
    ClinicQueueSummary *summary)
{
    static const char GET_CURRENT_TICKET_SQL[] =
        "SELECT id,user_id,department_id,queue_number,status,service_date,"
        "created_time,called_time FROM tickets "
        "WHERE user_id = ? AND service_date = ? "
        "ORDER BY id DESC LIMIT 1;";
    ClinicStoreSqliteContext *sqlite_context = context;
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_DATABASE_ERROR;
    ClinicTicket result_ticket;
    ClinicQueueSummary result_summary;
    char service_date[CLINIC_SERVICE_DATE_LENGTH + 1U];
    int64_t current_time = 0;
    int result;

    if (ticket != NULL)
    {
        memset(ticket, 0, sizeof(*ticket));
    }
    if (summary != NULL)
    {
        memset(summary, 0, sizeof(*summary));
    }
    if (sqlite_context == NULL || sqlite_context->database == NULL ||
        user_id <= 0 || ticket == NULL || summary == NULL)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    memset(&result_ticket, 0, sizeof(result_ticket));
    memset(&result_summary, 0, sizeof(result_summary));

    status = query_ticket_clock(
        sqlite_context->database,
        service_date,
        &current_time);
    if (status != CLINIC_STORE_OK)
    {
        return status;
    }

    result = sqlite3_prepare_v2(
        sqlite_context->database,
        GET_CURRENT_TICKET_SQL,
        -1,
        &statement,
        NULL);
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(
            statement,
            1,
            (sqlite3_int64)user_id);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_text(
            statement,
            2,
            service_date,
            -1,
            SQLITE_TRANSIENT);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_step(statement);
        if (result == SQLITE_ROW)
        {
            status = copy_ticket_from_statement(statement, &result_ticket);
        }
        else if (result == SQLITE_DONE)
        {
            status = CLINIC_STORE_CURRENT_TICKET_NOT_FOUND;
        }
    }
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    if (status == CLINIC_STORE_OK)
    {
        status = query_current_called_queue_number(
            sqlite_context->database,
            result_ticket.department_id,
            result_ticket.service_date,
            &result_summary.current_called_queue_number);
    }
    if (status == CLINIC_STORE_OK &&
        result_ticket.status == CLINIC_TICKET_WAITING)
    {
        status = query_waiting_ahead_count(
            sqlite_context->database,
            result_ticket.department_id,
            result_ticket.service_date,
            result_ticket.queue_number,
            &result_summary.waiting_ahead_count);
    }
    if (status == CLINIC_STORE_OK)
    {
        *ticket = result_ticket;
        *summary = result_summary;
    }
    return status;
}

/* 按 queue_number ASC、id ASC 找指定科室当天最早的一张目标状态号单。 */
static ClinicStoreStatus find_department_ticket_by_status(
    sqlite3 *database,
    int64_t department_id,
    const char *service_date,
    ClinicTicketStatus ticket_status,
    ClinicTicket *ticket)
{
    static const char SQL[] =
        "SELECT id,user_id,department_id,queue_number,status,service_date,"
        "created_time,called_time FROM tickets "
        "WHERE department_id = ? AND service_date = ? AND status = ? "
        "ORDER BY queue_number ASC, id ASC LIMIT 1;";
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_DATABASE_ERROR;
    int result = sqlite3_prepare_v2(database, SQL, -1, &statement, NULL);

    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(
            statement,
            1,
            (sqlite3_int64)department_id);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_text(
            statement,
            2,
            service_date,
            -1,
            SQLITE_TRANSIENT);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int(statement, 3, (int)ticket_status);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_step(statement);
        if (result == SQLITE_ROW)
        {
            status = copy_ticket_from_statement(statement, ticket);
        }
        else if (result == SQLITE_DONE)
        {
            status = CLINIC_STORE_NOT_FOUND;
        }
    }
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    return status;
}

/*
 * 条件更新 WHERE id=? AND status=WAITING，且要求 sqlite3_changes()==1。
 * 即使并发情况下号单状态已被别人改变，也不会把非 WAITING 号单重复叫号。
 */
static ClinicStoreStatus update_ticket_to_called(
    sqlite3 *database,
    int64_t ticket_id,
    int64_t called_time)
{
    static const char SQL[] =
        "UPDATE tickets SET status = ?, called_time = ? "
        "WHERE id = ? AND status = ?;";
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_DATABASE_ERROR;
    int result = sqlite3_prepare_v2(database, SQL, -1, &statement, NULL);

    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int(statement, 1, CLINIC_TICKET_CALLED);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(
            statement,
            2,
            (sqlite3_int64)called_time);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(
            statement,
            3,
            (sqlite3_int64)ticket_id);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int(statement, 4, CLINIC_TICKET_WAITING);
    }
    if (result == SQLITE_OK && sqlite3_step(statement) == SQLITE_DONE &&
        sqlite3_changes(database) == 1)
    {
        status = CLINIC_STORE_OK;
    }
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    return status;
}

/* 重复取号检查：同用户、同科室、同日期，状态为 WAITING 或 CALLED 都算有效号单。 */
static ClinicStoreStatus find_active_ticket(
    sqlite3 *database,
    int64_t user_id,
    int64_t department_id,
    const char *service_date,
    ClinicTicket *ticket)
{
    static const char SQL[] =
        "SELECT id,user_id,department_id,queue_number,status,service_date,"
        "created_time,called_time FROM tickets "
        "WHERE user_id=? AND department_id=? AND service_date=? "
        "AND status IN (0,1) ORDER BY id LIMIT 1;";
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_DATABASE_ERROR;
    int result = sqlite3_prepare_v2(database, SQL, -1, &statement, NULL);

    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(statement, 1, (sqlite3_int64)user_id);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(statement, 2, (sqlite3_int64)department_id);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_text(statement, 3, service_date, -1, SQLITE_TRANSIENT);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_step(statement);
        if (result == SQLITE_ROW)
        {
            status = copy_ticket_from_statement(statement, ticket);
        }
        else if (result == SQLITE_DONE)
        {
            status = CLINIC_STORE_NOT_FOUND;
        }
    }
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    return status;
}

/* 在事务中读取当天该科室 MAX(queue_number)+1，生成下一排队号码。 */
static ClinicStoreStatus query_next_queue_number(
    sqlite3 *database,
    int64_t department_id,
    const char *service_date,
    int64_t *queue_number)
{
    static const char SQL[] =
        "SELECT COALESCE(MAX(queue_number),0)+1 FROM tickets "
        "WHERE department_id=? AND service_date=?;";
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_DATABASE_ERROR;
    int result = sqlite3_prepare_v2(database, SQL, -1, &statement, NULL);

    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(statement, 1, (sqlite3_int64)department_id);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_text(statement, 2, service_date, -1, SQLITE_TRANSIENT);
    }
    if (result == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW)
    {
        *queue_number = (int64_t)sqlite3_column_int64(statement, 0);
        if (*queue_number > 0)
        {
            status = CLINIC_STORE_OK;
        }
    }
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    return status;
}

/* 插入初始状态为 WAITING 的新号单，并读取 SQLite 生成的自增 ticket id。 */
static ClinicStoreStatus insert_ticket(
    sqlite3 *database,
    ClinicTicket *ticket)
{
    static const char SQL[] =
        "INSERT INTO tickets(user_id,department_id,queue_number,status,"
        "service_date,created_time,called_time) VALUES(?,?,?,?,?,?,NULL);";
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_DATABASE_ERROR;
    int result = sqlite3_prepare_v2(database, SQL, -1, &statement, NULL);

    if (result == SQLITE_OK) result = sqlite3_bind_int64(statement, 1, ticket->user_id);
    if (result == SQLITE_OK) result = sqlite3_bind_int64(statement, 2, ticket->department_id);
    if (result == SQLITE_OK) result = sqlite3_bind_int64(statement, 3, ticket->queue_number);
    if (result == SQLITE_OK) result = sqlite3_bind_int(statement, 4, ticket->status);
    if (result == SQLITE_OK) result = sqlite3_bind_text(statement, 5, ticket->service_date, -1, SQLITE_TRANSIENT);
    if (result == SQLITE_OK) result = sqlite3_bind_int64(statement, 6, ticket->created_time);
    if (result == SQLITE_OK && sqlite3_step(statement) == SQLITE_DONE)
    {
        ticket->id = (int64_t)sqlite3_last_insert_rowid(database);
        status = ticket->id > 0 ? CLINIC_STORE_OK : CLINIC_STORE_DATABASE_ERROR;
    }
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    return status;
}

/*
 * 取号事务完整流程：
 * 1. 验证 user_id、department_id 存在；2. BEGIN IMMEDIATE 取得写锁；
 * 3. 读取服务器日期；4. 查找已有有效号单，有则返回原号单；
 * 5. 计算下一号码并 INSERT；6. COMMIT。
 * 任一步失败都 ROLLBACK。事务与唯一索引共同防止两个并发请求生成重复有效号单。
 */
static ClinicStoreStatus sqlite_create_ticket(
    void *context,
    int64_t user_id,
    int64_t department_id,
    ClinicTicket *ticket)
{
    static const char USER_EXISTS_SQL[] = "SELECT 1 FROM users WHERE id=?;";
    static const char DEPARTMENT_EXISTS_SQL[] = "SELECT 1 FROM departments WHERE id=?;";
    ClinicStoreSqliteContext *sqlite_context = context;
    ClinicStoreStatus status;
    char service_date[CLINIC_SERVICE_DATE_LENGTH + 1U];
    int64_t created_time = 0;
    int exists;

    if (sqlite_context == NULL || sqlite_context->database == NULL ||
        user_id <= 0 || department_id <= 0 || ticket == NULL)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    memset(ticket, 0, sizeof(*ticket));
    if (execute_sql(sqlite_context->database, "BEGIN IMMEDIATE;") != SQLITE_OK)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }
    status = query_id_exists(sqlite_context->database, USER_EXISTS_SQL, user_id, &exists);
    if (status == CLINIC_STORE_OK && !exists) status = CLINIC_STORE_USER_NOT_FOUND;
    if (status == CLINIC_STORE_OK)
    {
        status = query_id_exists(sqlite_context->database, DEPARTMENT_EXISTS_SQL, department_id, &exists);
        if (status == CLINIC_STORE_OK && !exists) status = CLINIC_STORE_DEPARTMENT_NOT_FOUND;
    }
    if (status == CLINIC_STORE_OK)
    {
        status = query_ticket_clock(
            sqlite_context->database,
            service_date,
            &created_time);
    }
    if (status == CLINIC_STORE_OK)
    {
        status = find_active_ticket(
            sqlite_context->database,
            user_id,
            department_id,
            service_date,
            ticket);
        if (status == CLINIC_STORE_OK)
        {
            if (execute_sql(sqlite_context->database, "COMMIT;") == SQLITE_OK)
            {
                return CLINIC_STORE_ACTIVE_TICKET_EXISTS;
            }
            status = CLINIC_STORE_DATABASE_ERROR;
        }
        else if (status == CLINIC_STORE_NOT_FOUND)
        {
            memset(ticket, 0, sizeof(*ticket));
            ticket->user_id = user_id;
            ticket->department_id = department_id;
            ticket->status = CLINIC_TICKET_WAITING;
            memcpy(
                ticket->service_date,
                service_date,
                CLINIC_SERVICE_DATE_LENGTH + 1U);
            ticket->created_time = created_time;
            status = query_next_queue_number(
                sqlite_context->database,
                department_id,
                ticket->service_date,
                &ticket->queue_number);
            if (status == CLINIC_STORE_OK) status = insert_ticket(sqlite_context->database, ticket);
            if (status == CLINIC_STORE_OK && execute_sql(sqlite_context->database, "COMMIT;") == SQLITE_OK) return CLINIC_STORE_OK;
            status = CLINIC_STORE_DATABASE_ERROR;
        }
    }
    (void)execute_sql(sqlite_context->database, "ROLLBACK;");
    memset(ticket, 0, sizeof(*ticket));
    return status;
}

/*
 * 叫号事务完整流程：BEGIN IMMEDIATE -> 验证科室 -> 获取服务器时间 ->
 * 找最早 WAITING -> 条件更新为 CALLED -> 重新查询更新后的号单 -> COMMIT。
 * 没有 WAITING 号单返回 NO_WAITING_TICKET，不会把以前的 CALLED 号单重复返回。
 */
static ClinicStoreStatus sqlite_call_next(
    void *context,
    int64_t department_id,
    ClinicTicket *ticket)
{
    static const char DEPARTMENT_EXISTS_SQL[] =
        "SELECT 1 FROM departments WHERE id=?;";
    ClinicStoreSqliteContext *sqlite_context = context;
    ClinicStoreStatus status;
    ClinicTicket result_ticket;
    char service_date[CLINIC_SERVICE_DATE_LENGTH + 1U];
    int64_t called_time = 0;
    int64_t selected_ticket_id;
    int exists;

    if (ticket != NULL)
    {
        memset(ticket, 0, sizeof(*ticket));
    }
    if (sqlite_context == NULL || sqlite_context->database == NULL ||
        department_id <= 0 || ticket == NULL)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    memset(&result_ticket, 0, sizeof(result_ticket));
    if (execute_sql(sqlite_context->database, "BEGIN IMMEDIATE;") != SQLITE_OK)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }

    status = query_id_exists(
        sqlite_context->database,
        DEPARTMENT_EXISTS_SQL,
        department_id,
        &exists);
    if (status == CLINIC_STORE_OK && !exists)
    {
        status = CLINIC_STORE_DEPARTMENT_NOT_FOUND;
    }
    if (status == CLINIC_STORE_OK)
    {
        status = query_ticket_clock(
            sqlite_context->database,
            service_date,
            &called_time);
    }
    if (status == CLINIC_STORE_OK)
    {
        status = find_department_ticket_by_status(
            sqlite_context->database,
            department_id,
            service_date,
            CLINIC_TICKET_WAITING,
            &result_ticket);
        if (status == CLINIC_STORE_NOT_FOUND)
        {
            status = CLINIC_STORE_NO_WAITING_TICKET;
        }
        else if (status == CLINIC_STORE_OK)
        {
            selected_ticket_id = result_ticket.id;
            status = update_ticket_to_called(
                sqlite_context->database,
                selected_ticket_id,
                called_time);
            if (status == CLINIC_STORE_OK)
            {
                memset(&result_ticket, 0, sizeof(result_ticket));
                status = sqlite_get_ticket(
                    sqlite_context,
                    selected_ticket_id,
                    &result_ticket);
                if (status != CLINIC_STORE_OK)
                {
                    status = CLINIC_STORE_DATABASE_ERROR;
                }
            }
        }
    }
    if (status == CLINIC_STORE_OK &&
        execute_sql(sqlite_context->database, "COMMIT;") == SQLITE_OK)
    {
        *ticket = result_ticket;
        return CLINIC_STORE_OK;
    }
    if (status == CLINIC_STORE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    (void)execute_sql(sqlite_context->database, "ROLLBACK;");
    memset(ticket, 0, sizeof(*ticket));
    return status;
}

/* 注册使用参数绑定写入用户；UNIQUE(username) 冲突映射成 DUPLICATE。 */
static ClinicStoreStatus sqlite_create_user(
    void *context,
    const char *username,
    const char *password,
    int64_t *user_id)
{
    static const char INSERT_USER_SQL[] =
        "INSERT INTO users (username, password) VALUES (?, ?);";
    ClinicStoreSqliteContext *sqlite_context = context;
    sqlite3_stmt *statement = NULL;
    int result;

    if (sqlite_context == NULL || sqlite_context->database == NULL ||
        username == NULL || password == NULL || user_id == NULL)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }

    result = sqlite3_prepare_v2(
        sqlite_context->database,
        INSERT_USER_SQL,
        -1,
        &statement,
        NULL);
    if (result != SQLITE_OK)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }

    result = sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
    if (result == SQLITE_OK)
    {
        /* Plaintext password: teaching demo only; never suitable for production. */
        result = sqlite3_bind_text(statement, 2, password, -1, SQLITE_TRANSIENT);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_step(statement);
    }

    if (result == SQLITE_DONE)
    {
        *user_id = (int64_t)sqlite3_last_insert_rowid(sqlite_context->database);
        result = SQLITE_OK;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK && result == SQLITE_OK)
    {
        result = SQLITE_ERROR;
    }

    if (result == SQLITE_OK)
    {
        return CLINIC_STORE_OK;
    }
    if (result == SQLITE_CONSTRAINT ||
        result == SQLITE_CONSTRAINT_UNIQUE ||
        result == SQLITE_CONSTRAINT_PRIMARYKEY)
    {
        return CLINIC_STORE_DUPLICATE;
    }
    return CLINIC_STORE_DATABASE_ERROR;
}

static ClinicStoreStatus copy_user_from_statement(
    sqlite3_stmt *statement,
    ClinicStoredUser *user)
{
    const unsigned char *username = sqlite3_column_text(statement, 1);
    const unsigned char *password = sqlite3_column_text(statement, 2);
    int username_length = sqlite3_column_bytes(statement, 1);
    int password_length = sqlite3_column_bytes(statement, 2);

    if (username == NULL || password == NULL ||
        username_length < 0 || password_length < 0 ||
        (size_t)username_length > CLINIC_USERNAME_MAX_LENGTH ||
        (size_t)password_length > CLINIC_PASSWORD_MAX_LENGTH)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }

    user->id = (int64_t)sqlite3_column_int64(statement, 0);
    if (user->id <= 0)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }

    memcpy(user->username, username, (size_t)username_length);
    user->username[username_length] = '\0';
    memcpy(user->password, password, (size_t)password_length);
    user->password[password_length] = '\0';
    return CLINIC_STORE_OK;
}

/* 登录的数据访问部分只负责按用户名取记录；密码是否匹配由 Core 判断。 */
static ClinicStoreStatus sqlite_find_user_by_username(
    void *context,
    const char *username,
    ClinicStoredUser *user)
{
    static const char FIND_USER_SQL[] =
        "SELECT id, username, password FROM users WHERE username = ?;";
    ClinicStoreSqliteContext *sqlite_context = context;
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status;
    int result;

    if (sqlite_context == NULL || sqlite_context->database == NULL ||
        username == NULL || user == NULL)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }

    result = sqlite3_prepare_v2(
        sqlite_context->database,
        FIND_USER_SQL,
        -1,
        &statement,
        NULL);
    if (result != SQLITE_OK)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }

    result = sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
    if (result == SQLITE_OK)
    {
        result = sqlite3_step(statement);
    }

    if (result == SQLITE_ROW)
    {
        status = copy_user_from_statement(statement, user);
    }
    else if (result == SQLITE_DONE)
    {
        status = CLINIC_STORE_NOT_FOUND;
    }
    else
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK &&
        status != CLINIC_STORE_DATABASE_ERROR)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    return status;
}

/* 按 id 顺序读取科室；超过调用者数组容量时明确返回 CAPACITY_EXCEEDED。 */
static ClinicStoreStatus sqlite_list_departments(
    void *context,
    ClinicDepartment *departments,
    size_t capacity,
    size_t *count)
{
    static const char LIST_DEPARTMENTS_SQL[] =
        "SELECT id, name FROM departments ORDER BY id ASC;";
    ClinicStoreSqliteContext *sqlite_context = context;
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_OK;
    size_t result_count = 0U;
    int result;

    if (sqlite_context == NULL || sqlite_context->database == NULL ||
        departments == NULL || count == NULL || capacity == 0U)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    *count = 0U;

    result = sqlite3_prepare_v2(
        sqlite_context->database,
        LIST_DEPARTMENTS_SQL,
        -1,
        &statement,
        NULL);
    if (result != SQLITE_OK)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }

    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        const unsigned char *name = sqlite3_column_text(statement, 1);
        int name_length = sqlite3_column_bytes(statement, 1);
        int64_t department_id = (int64_t)sqlite3_column_int64(statement, 0);

        if (result_count >= capacity)
        {
            status = CLINIC_STORE_CAPACITY_EXCEEDED;
            break;
        }
        if (department_id <= 0 || name == NULL || name_length < 0 ||
            (size_t)name_length > CLINIC_DEPARTMENT_NAME_MAX_LENGTH)
        {
            status = CLINIC_STORE_DATABASE_ERROR;
            break;
        }

        departments[result_count].id = department_id;
        memcpy(
            departments[result_count].name,
            name,
            (size_t)name_length);
        departments[result_count].name[name_length] = '\0';
        ++result_count;
    }

    if (status == CLINIC_STORE_OK && result != SQLITE_DONE)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    if (sqlite3_finalize(statement) != SQLITE_OK &&
        status != CLINIC_STORE_DATABASE_ERROR)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    if (status == CLINIC_STORE_OK)
    {
        *count = result_count;
    }
    return status;
}

static ClinicStoreStatus copy_doctor_from_statement(
    sqlite3_stmt *statement,
    ClinicDoctor *doctor)
{
    const unsigned char *name = sqlite3_column_text(statement, 2);
    const unsigned char *title = sqlite3_column_text(statement, 3);
    const unsigned char *specialty = sqlite3_column_text(statement, 4);
    int name_length = sqlite3_column_bytes(statement, 2);
    int title_length = sqlite3_column_bytes(statement, 3);
    int specialty_length = sqlite3_column_bytes(statement, 4);

    if (name == NULL || title == NULL || specialty == NULL ||
        name_length < 0 || title_length < 0 || specialty_length < 0 ||
        (size_t)name_length > CLINIC_DOCTOR_NAME_MAX_LENGTH ||
        (size_t)title_length > CLINIC_DOCTOR_TITLE_MAX_LENGTH ||
        (size_t)specialty_length > CLINIC_DOCTOR_SPECIALTY_MAX_LENGTH)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }

    doctor->id = (int64_t)sqlite3_column_int64(statement, 0);
    doctor->department_id = (int64_t)sqlite3_column_int64(statement, 1);
    if (doctor->id <= 0 || doctor->department_id <= 0)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }

    memcpy(doctor->name, name, (size_t)name_length);
    doctor->name[name_length] = '\0';
    memcpy(doctor->title, title, (size_t)title_length);
    doctor->title[title_length] = '\0';
    memcpy(doctor->specialty, specialty, (size_t)specialty_length);
    doctor->specialty[specialty_length] = '\0';
    return CLINIC_STORE_OK;
}

/* 使用 department_id 参数绑定筛选医生，返回姓名、职称和擅长方向。 */
static ClinicStoreStatus sqlite_list_doctors(
    void *context,
    int64_t department_id,
    ClinicDoctor *doctors,
    size_t capacity,
    size_t *count)
{
    static const char LIST_DOCTORS_SQL[] =
        "SELECT id, department_id, name, title, specialty "
        "FROM doctors WHERE department_id = ? ORDER BY id ASC;";
    ClinicStoreSqliteContext *sqlite_context = context;
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_OK;
    size_t result_count = 0U;
    int result;

    if (sqlite_context == NULL || sqlite_context->database == NULL ||
        department_id <= 0 || doctors == NULL || count == NULL ||
        capacity == 0U)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    *count = 0U;

    result = sqlite3_prepare_v2(
        sqlite_context->database,
        LIST_DOCTORS_SQL,
        -1,
        &statement,
        NULL);
    if (result != SQLITE_OK)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }

    result = sqlite3_bind_int64(statement, 1, (sqlite3_int64)department_id);
    if (result != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    else
    {
        while ((result = sqlite3_step(statement)) == SQLITE_ROW)
        {
            if (result_count >= capacity)
            {
                status = CLINIC_STORE_CAPACITY_EXCEEDED;
                break;
            }
            status = copy_doctor_from_statement(
                statement,
                &doctors[result_count]);
            if (status != CLINIC_STORE_OK)
            {
                break;
            }
            ++result_count;
        }
    }

    if (status == CLINIC_STORE_OK && result != SQLITE_DONE)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    if (sqlite3_finalize(statement) != SQLITE_OK &&
        status != CLINIC_STORE_DATABASE_ERROR)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    if (status == CLINIC_STORE_OK)
    {
        *count = result_count;
    }
    return status;
}

/* 使用 id 游标分页读取用户摘要；多查询一行用于判断 has_more。 */
static ClinicStoreStatus sqlite_list_users(
    void *context,
    int64_t after_id,
    ClinicUserSummary *users,
    size_t capacity,
    size_t *count,
    int *has_more)
{
    static const char SQL[] =
        "SELECT id,username FROM users WHERE id > ? "
        "ORDER BY id ASC LIMIT ?;";
    ClinicStoreSqliteContext *sqlite_context = context;
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_DATABASE_ERROR;
    size_t result_count = 0U;
    int result;

    if (sqlite_context == NULL || sqlite_context->database == NULL ||
        after_id < 0 || users == NULL || count == NULL || has_more == NULL ||
        capacity == 0U || capacity > CLINIC_ADMIN_PAGE_MAX_ITEMS)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    *count = 0U;
    *has_more = 0;
    result = sqlite3_prepare_v2(
        sqlite_context->database,
        SQL,
        -1,
        &statement,
        NULL);
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(statement, 1, (sqlite3_int64)after_id);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int(statement, 2, (int)(capacity + 1U));
    }
    if (result == SQLITE_OK)
    {
        status = CLINIC_STORE_OK;
        while ((result = sqlite3_step(statement)) == SQLITE_ROW)
        {
            const unsigned char *username;
            int username_length;

            if (result_count == capacity)
            {
                *has_more = 1;
                result = SQLITE_DONE;
                break;
            }
            username = sqlite3_column_text(statement, 1);
            username_length = sqlite3_column_bytes(statement, 1);
            users[result_count].id =
                (int64_t)sqlite3_column_int64(statement, 0);
            if (users[result_count].id <= 0 || username == NULL ||
                username_length <= 0 ||
                username_length > (int)CLINIC_USERNAME_MAX_LENGTH)
            {
                status = CLINIC_STORE_DATABASE_ERROR;
                break;
            }
            memcpy(
                users[result_count].username,
                username,
                (size_t)username_length);
            users[result_count].username[username_length] = '\0';
            ++result_count;
        }
        if (status == CLINIC_STORE_OK && result != SQLITE_DONE)
        {
            status = CLINIC_STORE_DATABASE_ERROR;
        }
    }
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    if (status == CLINIC_STORE_OK)
    {
        *count = result_count;
    }
    return status;
}

/* 分页读取号单，并在服务器端 JOIN 用户名和科室名供管理台展示。 */
static ClinicStoreStatus sqlite_list_tickets(
    void *context,
    int64_t after_id,
    ClinicAdminTicketRecord *tickets,
    size_t capacity,
    size_t *count,
    int *has_more)
{
    static const char SQL[] =
        "SELECT t.id,t.user_id,t.department_id,t.queue_number,t.status,"
        "t.service_date,t.created_time,t.called_time,u.username,d.name "
        "FROM tickets AS t "
        "JOIN users AS u ON u.id=t.user_id "
        "JOIN departments AS d ON d.id=t.department_id "
        "WHERE t.id > ? ORDER BY t.id ASC LIMIT ?;";
    ClinicStoreSqliteContext *sqlite_context = context;
    sqlite3_stmt *statement = NULL;
    ClinicStoreStatus status = CLINIC_STORE_DATABASE_ERROR;
    size_t result_count = 0U;
    int result;

    if (sqlite_context == NULL || sqlite_context->database == NULL ||
        after_id < 0 || tickets == NULL || count == NULL || has_more == NULL ||
        capacity == 0U || capacity > CLINIC_ADMIN_PAGE_MAX_ITEMS)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    *count = 0U;
    *has_more = 0;
    result = sqlite3_prepare_v2(
        sqlite_context->database,
        SQL,
        -1,
        &statement,
        NULL);
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(statement, 1, (sqlite3_int64)after_id);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int(statement, 2, (int)(capacity + 1U));
    }
    if (result == SQLITE_OK)
    {
        status = CLINIC_STORE_OK;
        while ((result = sqlite3_step(statement)) == SQLITE_ROW)
        {
            const unsigned char *username;
            const unsigned char *department_name;
            int username_length;
            int department_name_length;

            if (result_count == capacity)
            {
                *has_more = 1;
                result = SQLITE_DONE;
                break;
            }
            status = copy_ticket_from_statement(
                statement,
                &tickets[result_count].ticket);
            username = sqlite3_column_text(statement, 8);
            username_length = sqlite3_column_bytes(statement, 8);
            department_name = sqlite3_column_text(statement, 9);
            department_name_length = sqlite3_column_bytes(statement, 9);
            if (status != CLINIC_STORE_OK || username == NULL ||
                username_length <= 0 ||
                username_length > (int)CLINIC_USERNAME_MAX_LENGTH ||
                department_name == NULL || department_name_length <= 0 ||
                department_name_length >
                    (int)CLINIC_DEPARTMENT_NAME_MAX_LENGTH)
            {
                status = CLINIC_STORE_DATABASE_ERROR;
                break;
            }
            memcpy(
                tickets[result_count].username,
                username,
                (size_t)username_length);
            tickets[result_count].username[username_length] = '\0';
            memcpy(
                tickets[result_count].department_name,
                department_name,
                (size_t)department_name_length);
            tickets[result_count].department_name[department_name_length] = '\0';
            ++result_count;
        }
        if (status == CLINIC_STORE_OK && result != SQLITE_DONE)
        {
            status = CLINIC_STORE_DATABASE_ERROR;
        }
    }
    if (statement != NULL && sqlite3_finalize(statement) != SQLITE_OK)
    {
        status = CLINIC_STORE_DATABASE_ERROR;
    }
    if (status == CLINIC_STORE_OK)
    {
        *count = result_count;
    }
    return status;
}

/* 关闭 sqlite3 连接并释放实现上下文；成功后外层 Store 才会解绑函数表。 */
static ClinicStoreStatus sqlite_close_store(void *context)
{
    ClinicStoreSqliteContext *sqlite_context = context;
    int result;

    if (sqlite_context == NULL || sqlite_context->database == NULL)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }

    result = sqlite3_close(sqlite_context->database);
    if (result != SQLITE_OK)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }

    sqlite_context->database = NULL;
    free(sqlite_context);
    return CLINIC_STORE_OK;
}

static const ClinicStoreOperations SQLITE_OPERATIONS = {
    .create_user = sqlite_create_user,
    .find_user_by_username = sqlite_find_user_by_username,
    .list_departments = sqlite_list_departments,
    .list_doctors = sqlite_list_doctors,
    .close = sqlite_close_store,
    .create_ticket = sqlite_create_ticket,
    .get_ticket = sqlite_get_ticket,
    .get_current_ticket = sqlite_get_current_ticket,
    .call_next = sqlite_call_next,
    .list_users = sqlite_list_users,
    .list_tickets = sqlite_list_tickets
};

/*
 * SQLite Store 的公开装配入口：分配上下文、sqlite3_open、启用外键、执行建表/种子 SQL，
 * 最后把 SQLITE_OPERATIONS 和 context 填入 ClinicStore。中途失败会关闭并释放所有资源。
 */
ClinicStoreStatus clinic_store_sqlite_open(
    ClinicStore *store,
    const char *database_path)
{
    ClinicStoreSqliteContext *context;
    char *error_message = NULL;
    int result;

    if (store == NULL || database_path == NULL || database_path[0] == '\0' ||
        store->operations != NULL || store->context != NULL)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }

    context = malloc(sizeof(*context));
    if (context == NULL)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }
    context->database = NULL;

    result = sqlite3_open_v2(
        database_path,
        &context->database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
        NULL);
    if (result != SQLITE_OK)
    {
        if (context->database != NULL)
        {
            (void)sqlite3_close(context->database);
        }
        free(context);
        return CLINIC_STORE_DATABASE_ERROR;
    }

    (void)sqlite3_extended_result_codes(context->database, 1);
    (void)sqlite3_busy_timeout(context->database, 5000);
    if (execute_sql(context->database, "PRAGMA foreign_keys=ON;") != SQLITE_OK)
    {
        (void)sqlite3_close(context->database);
        free(context);
        return CLINIC_STORE_DATABASE_ERROR;
    }
    result = sqlite3_exec(
        context->database,
        INITIALIZE_SCHEMA_SQL,
        NULL,
        NULL,
        &error_message);
    sqlite3_free(error_message);
    if (result != SQLITE_OK)
    {
        (void)sqlite3_exec(
            context->database,
            "ROLLBACK;",
            NULL,
            NULL,
            NULL);
        (void)sqlite3_close(context->database);
        free(context);
        return CLINIC_STORE_DATABASE_ERROR;
    }

    store->operations = &SQLITE_OPERATIONS;
    store->context = context;
    return CLINIC_STORE_OK;
}
