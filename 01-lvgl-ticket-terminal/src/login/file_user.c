/**
 * @file file_user.c
 * @brief 用户账号文件读写模块 - 实现
 * 
 * 依赖说明:
 *   - 依赖标准C库: stdio.h, string.h, stdlib.h
 *   - 文件路径基于开发板根目录, 无SD卡通过根文件系统持久化
 *   - user.txt 格式: 每行 "账号@密码\n"
 *   - save_pwd.txt 格式: 单行 "账号@密码\n" 或空文件
 */

#include "file_user.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ==================== 内部辅助函数 ==================== */

/**
 * @brief 从一行 "name@pwd" 格式字符串中解析出用户名和密码
 * @param line  输入行字符串(不含换行符)
 * @param name  输出用户名缓冲区
 * @param pwd   输出密码缓冲区
 * @param max   缓冲区最大长度
 * @return 0=成功, -1=格式错误(缺少@分隔符)
 */
static int parse_user_line(const char *line, char *name, char *pwd, int max)
{
    const char *at_pos = strchr(line, '@');
    if (at_pos == NULL || at_pos == line) {
        return -1;  /* 缺少@分隔符 或 用户名为空 */
    }
    
    int name_len = (int)(at_pos - line);
    if (name_len >= max) name_len = max - 1;
    memcpy(name, line, name_len);
    name[name_len] = '\0';
    
    const char *pwd_start = at_pos + 1;
    int pwd_len = (int)strlen(pwd_start);
    if (pwd_len >= max) pwd_len = max - 1;
    memcpy(pwd, pwd_start, pwd_len);
    pwd[pwd_len] = '\0';
    
    return 0;
}

/* ==================== 用户账号文件操作 ==================== */

int file_user_read_all(char *out_buf, int buf_size)
{
    if (out_buf == NULL || buf_size <= 0) return 0;
    
    FILE *fp = fopen(USER_FILE_PATH, "r");
    if (fp == NULL) {
        return 0;  /* 文件不存在视为空, 不是错误 */
    }
    
    int total = 0;
    int pos   = 0;
    char line[USER_LINE_MAX];
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        /* 跳过空行 */
        if (line[0] == '\n' || line[0] == '\0') continue;
        
        int len = (int)strlen(line);
        /* 去掉尾部换行符 */
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        if (len == 0) continue;
        
        /* 检查是否有@分隔符, 格式有效才算 */
        if (strchr(line, '@') == NULL) continue;
        
        /* 写入输出缓冲区 */
        if (pos + len < buf_size) {
            if (pos > 0) {
                out_buf[pos++] = '\n';
            }
            memcpy(out_buf + pos, line, len);
            pos += len;
            total++;
        }
    }
    out_buf[pos] = '\0';
    
    fclose(fp);
    return total;
}

int file_user_check_exist(const char *name)
{
    if (name == NULL || name[0] == '\0') return -1;
    
    FILE *fp = fopen(USER_FILE_PATH, "r");
    if (fp == NULL) {
        return 0;  /* 文件不存在 -> 用户不存在 */
    }
    
    char line[USER_LINE_MAX];
    char tmp_name[USER_NAME_MAX];
    char tmp_pwd[USER_PWD_MAX];
    int  found = 0;
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (line[0] == '\n' || line[0] == '\0') continue;
        
        int len = (int)strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        
        if (parse_user_line(line, tmp_name, tmp_pwd, sizeof(tmp_name)) == 0) {
            if (strcmp(tmp_name, name) == 0) {
                found = 1;
                break;
            }
        }
    }
    
    fclose(fp);
    return found;
}

int file_user_register(const char *name, const char *pwd)
{
    if (name == NULL || pwd == NULL || name[0] == '\0' || pwd[0] == '\0') {
        return -1;
    }
    
    /* 禁止用户名或密码中包含 @ */
    if (strchr(name, '@') != NULL || strchr(pwd, '@') != NULL) {
        return -1;
    }
    
    /* 查重 */
    if (file_user_check_exist(name) == 1) {
        return -2;  /* 用户名已存在 */
    }
    
    /* 追加写入 */
    FILE *fp = fopen(USER_FILE_PATH, "a");
    if (fp == NULL) {
        return -1;  /* 文件打开失败 */
    }
    
    fprintf(fp, "%s@%s\n", name, pwd);
    fflush(fp);          /* 强制刷新缓冲区, 确保持久化 */
    fclose(fp);
    
    return 0;
}

int file_user_login_check(const char *name, const char *pwd)
{
    if (name == NULL || pwd == NULL || name[0] == '\0' || pwd[0] == '\0') {
        return -1;
    }
    
    FILE *fp = fopen(USER_FILE_PATH, "r");
    if (fp == NULL) {
        return -1;  /* 文件不存在 -> 用户不存在 */
    }
    
    char line[USER_LINE_MAX];
    char tmp_name[USER_NAME_MAX];
    char tmp_pwd[USER_PWD_MAX];
    int  user_found = 0;
    int  result     = -1;
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (line[0] == '\n' || line[0] == '\0') continue;
        
        int len = (int)strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        
        if (parse_user_line(line, tmp_name, tmp_pwd, sizeof(tmp_name)) == 0) {
            if (strcmp(tmp_name, name) == 0) {
                user_found = 1;
                if (strcmp(tmp_pwd, pwd) == 0) {
                    result = 1;  /* 登录成功 */
                } else {
                    result = 0;  /* 密码错误 */
                }
                break;
            }
        }
    }
    
    fclose(fp);
    
    if (!user_found) {
        return -1;  /* 用户不存在 */
    }
    return result;
}

/* ==================== 记住密码缓存操作 ==================== */

int file_pwd_cache_read(char *out_name, char *out_pwd, int max_len)
{
    if (out_name == NULL || out_pwd == NULL || max_len <= 0) return 0;
    
    FILE *fp = fopen(SAVE_PWD_FILE_PATH, "r");
    if (fp == NULL) {
        return 0;  /* 文件不存在 -> 无缓存 */
    }
    
    char line[USER_LINE_MAX];
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return 0;  /* 空文件 */
    }
    fclose(fp);
    
    /* 去掉换行符 */
    int len = (int)strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
        len--;
    }
    if (len == 0) return 0;
    
    /* 解析 */
    if (parse_user_line(line, out_name, out_pwd, max_len) == 0) {
        if (out_name[0] != '\0' && out_pwd[0] != '\0') {
            return 1;  /* 有效缓存 */
        }
    }
    
    return -1;  /* 格式错误 */
}

int file_pwd_cache_write(const char *name, const char *pwd)
{
    /* 如果用户名为空，清空缓存 */
    if (name == NULL || name[0] == '\0') {
        return file_pwd_cache_clear();
    }
    
    FILE *fp = fopen(SAVE_PWD_FILE_PATH, "w");
    if (fp == NULL) {
        return -1;
    }
    
    fprintf(fp, "%s@%s\n", name, pwd);
    fflush(fp);
    fclose(fp);
    
    return 0;
}

int file_pwd_cache_clear(void)
{
    FILE *fp = fopen(SAVE_PWD_FILE_PATH, "w");
    if (fp == NULL) {
        return -1;
    }
    /* 以写模式打开再关闭 -> 清空文件 */
    fclose(fp);
    return 0;
}
