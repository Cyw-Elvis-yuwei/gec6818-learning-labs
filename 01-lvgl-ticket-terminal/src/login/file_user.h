/**
 * @file file_user.h
 * @brief 用户账号文件读写模块 - 头文件
 * 
 * 功能说明:
 *   - 通过开发板根目录 /user.txt 持久化存储账号密码(格式: 账号@密码 每行一条)
 *   - 通过 /save_pwd.txt 缓存记住密码数据
 *   - 提供查重、注册、登录校验、缓存读写全套接口
 */

#ifndef FILE_USER_H
#define FILE_USER_H

#include <stdint.h>

/* 文件路径宏定义 */
#define USER_FILE_PATH      "/user.txt"          /* 用户账号持久化文件 */
#define SAVE_PWD_FILE_PATH  "/save_pwd.txt"       /* 记住密码缓存文件 */

/* 缓冲区最大长度 */
#define USER_LINE_MAX       128    /* 单行 "账号@密码" 最大长度 */
#define USER_NAME_MAX       32     /* 用户名字段最大长度 */
#define USER_PWD_MAX        32     /* 密码字段最大长度 */

/**
 * @brief 读取全部用户数据，返回用户总数
 * @param out_buf  输出缓冲区，每行以 '\n' 分隔的字符串
 * @param buf_size 缓冲区大小
 * @return 读取到的用户行数，-1 表示文件打开失败
 * @note  文件不存在时返回 0(视为空)
 */
int file_user_read_all(char *out_buf, int buf_size);

/**
 * @brief 检查用户名是否已存在
 * @param name 用户名
 * @return 1=已存在, 0=不存在, -1=读取失败
 */
int file_user_check_exist(const char *name);

/**
 * @brief 注册新用户(追加写入)
 * @param name 用户名(不含@符号)
 * @param pwd  密码
 * @return 0=成功, -1=文件打开失败, -2=用户名已存在
 */
int file_user_register(const char *name, const char *pwd);

/**
 * @brief 登录校验：逐行匹配 user.txt
 * @param name 用户名
 * @param pwd  密码
 * @return 1=登录成功, 0=密码错误, -1=用户不存在, -2=文件错误
 */
int file_user_login_check(const char *name, const char *pwd);

/**
 * @brief 读取记住密码缓存
 * @param out_name 输出用户名字符串缓冲区
 * @param out_pwd  输出密码字符串缓冲区
 * @param max_len  每个缓冲区最大长度
 * @return 1=读取到有效缓存, 0=无缓存(文件不存在或为空), -1=格式错误
 */
int file_pwd_cache_read(char *out_name, char *out_pwd, int max_len);

/**
 * @brief 写入记住密码缓存
 * @param name 用户名(传NULL则清空缓存)
 * @param pwd  密码
 * @return 0=成功, -1=文件打开失败
 */
int file_pwd_cache_write(const char *name, const char *pwd);

/**
 * @brief 清空记住密码缓存文件
 * @return 0=成功, -1=失败
 */
int file_pwd_cache_clear(void);

#endif /* FILE_USER_H */
