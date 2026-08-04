/*
 * 文件作用（答辩）：实现登录页“记住密码”的板端本地凭据读写。
 * 用户选择记住后，登录成功才保存；取消记住并成功登录后删除，注册新账号后也会清理
 * 旧凭据。文件带版本标记并限制为当前用户读写权限（Linux 为 0600）。
 *
 * 安全边界：这是演示终端的本地持久化，不是硬件加密或密码保险库；服务器端账号数据
 * 仍由 SQLite 统一管理，临时密码缓冲区使用后会清零。
 *
 * 文件格式是“版本行 + 用户名行 + 密码行”。load 会校验版本、长度和多余内容；save
 * 先写临时文件、刷新并设置 0600 权限，再替换正式文件，减少写到一半留下坏文件的风险；
 * remove 用于用户取消记住或注册新账号后清理旧凭据。
 */
#ifdef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include "credential_store.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#define clinic_chmod _chmod
#define CLINIC_PRIVATE_FILE_MODE (_S_IREAD | _S_IWRITE)
#else
#define clinic_chmod chmod
#define CLINIC_PRIVATE_FILE_MODE (S_IRUSR | S_IWUSR)
#endif

#define CREDENTIAL_MAGIC "CLINIC-CREDENTIALS-V1"
#define CREDENTIAL_PATH_MAX_LENGTH 512U

static void clear_memory(void *memory, size_t length)
{
    volatile unsigned char *cursor = memory;

    while(length > 0U) {
        *cursor++ = 0U;
        --length;
    }
}

static int credential_text_is_valid(const char *text, size_t maximum_length)
{
    size_t length = 0U;

    if(text == NULL) {
        return 0;
    }
    while(length <= maximum_length && text[length] != '\0') {
        if(text[length] == '\n' || text[length] == '\r') {
            return 0;
        }
        ++length;
    }
    return length > 0U && length <= maximum_length;
}

static int read_line(
    FILE *file,
    char *buffer,
    size_t capacity)
{
    size_t length;

    if(file == NULL || buffer == NULL || capacity < 2U ||
       fgets(buffer, (int)capacity, file) == NULL) {
        return -1;
    }
    length = strlen(buffer);
    if(length == 0U || buffer[length - 1U] != '\n') {
        return -1;
    }
    buffer[length - 1U] = '\0';
    if(length > 1U && buffer[length - 2U] == '\r') {
        buffer[length - 2U] = '\0';
    }
    return 0;
}

/* 启动登录页时加载；任何格式异常都拒绝回填，并清零输出缓冲区。 */
ClinicCredentialStoreStatus clinic_credential_store_load(
    const char *path,
    ClinicRememberedCredentials *credentials)
{
    FILE *file;
    char magic[sizeof(CREDENTIAL_MAGIC) + 2U];
    char username[CLINIC_USERNAME_MAX_LENGTH + 3U];
    char password[CLINIC_PASSWORD_MAX_LENGTH + 3U];
    int trailing_character;
    ClinicCredentialStoreStatus status = CLINIC_CREDENTIAL_STORE_INVALID_DATA;

    if(path == NULL || path[0] == '\0' || credentials == NULL) {
        return CLINIC_CREDENTIAL_STORE_INVALID_ARGUMENT;
    }
    memset(credentials, 0, sizeof(*credentials));
    memset(magic, 0, sizeof(magic));
    memset(username, 0, sizeof(username));
    memset(password, 0, sizeof(password));

    file = fopen(path, "rb");
    if(file == NULL) {
        return errno == ENOENT
            ? CLINIC_CREDENTIAL_STORE_NOT_FOUND
            : CLINIC_CREDENTIAL_STORE_IO_ERROR;
    }

    if(read_line(file, magic, sizeof(magic)) == 0 &&
       strcmp(magic, CREDENTIAL_MAGIC) == 0 &&
       read_line(file, username, sizeof(username)) == 0 &&
       read_line(file, password, sizeof(password)) == 0 &&
       credential_text_is_valid(username, CLINIC_USERNAME_MAX_LENGTH) &&
       credential_text_is_valid(password, CLINIC_PASSWORD_MAX_LENGTH)) {
        trailing_character = fgetc(file);
        if(trailing_character == EOF && !ferror(file)) {
            memcpy(credentials->username, username, strlen(username) + 1U);
            memcpy(credentials->password, password, strlen(password) + 1U);
            status = CLINIC_CREDENTIAL_STORE_OK;
        }
    }
    if(fclose(file) != 0 && status == CLINIC_CREDENTIAL_STORE_OK) {
        status = CLINIC_CREDENTIAL_STORE_IO_ERROR;
    }
    if(status != CLINIC_CREDENTIAL_STORE_OK) {
        memset(credentials, 0, sizeof(*credentials));
    }
    clear_memory(password, sizeof(password));
    return status;
}

/* 仅在登录成功后调用；Linux 最终文件权限限制为当前用户读写（0600）。 */
ClinicCredentialStoreStatus clinic_credential_store_save(
    const char *path,
    const char *username,
    const char *password)
{
    FILE *file = NULL;
    char temporary_path[CREDENTIAL_PATH_MAX_LENGTH + 1U];
    int written;
    int failed = 0;

    if(path == NULL || path[0] == '\0' ||
       !credential_text_is_valid(username, CLINIC_USERNAME_MAX_LENGTH) ||
       !credential_text_is_valid(password, CLINIC_PASSWORD_MAX_LENGTH)) {
        return CLINIC_CREDENTIAL_STORE_INVALID_ARGUMENT;
    }
    written = snprintf(
        temporary_path,
        sizeof(temporary_path),
        "%s.tmp",
        path);
    if(written < 0 || (size_t)written >= sizeof(temporary_path)) {
        return CLINIC_CREDENTIAL_STORE_INVALID_ARGUMENT;
    }

    file = fopen(temporary_path, "wb");
    if(file == NULL) {
        return CLINIC_CREDENTIAL_STORE_IO_ERROR;
    }
    if(clinic_chmod(temporary_path, CLINIC_PRIVATE_FILE_MODE) != 0 ||
       fprintf(file, "%s\n%s\n%s\n", CREDENTIAL_MAGIC, username, password) < 0 ||
       fflush(file) != 0) {
        failed = 1;
    }
    if(fclose(file) != 0) {
        failed = 1;
    }
    if(failed) {
        (void)remove(temporary_path);
        return CLINIC_CREDENTIAL_STORE_IO_ERROR;
    }

#ifdef _WIN32
    (void)remove(path);
#endif
    if(rename(temporary_path, path) != 0 ||
       clinic_chmod(path, CLINIC_PRIVATE_FILE_MODE) != 0) {
        (void)remove(temporary_path);
        return CLINIC_CREDENTIAL_STORE_IO_ERROR;
    }
    return CLINIC_CREDENTIAL_STORE_OK;
}

/* 删除本地记住密码文件；文件本来不存在也视为目标已经达到。 */
ClinicCredentialStoreStatus clinic_credential_store_remove(
    const char *path)
{
    if(path == NULL || path[0] == '\0') {
        return CLINIC_CREDENTIAL_STORE_INVALID_ARGUMENT;
    }
    if(remove(path) == 0 || errno == ENOENT) {
        return CLINIC_CREDENTIAL_STORE_OK;
    }
    return CLINIC_CREDENTIAL_STORE_IO_ERROR;
}
