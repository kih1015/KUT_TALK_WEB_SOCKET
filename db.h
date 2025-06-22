#pragma once
#include <mysql/mysql.h>

int db_global_init(const char *h, const char *u,
                   const char *p, const char *s, unsigned port);

void db_global_end(void);

/* -------- 라이브러리 전역 초기화 / 종료 -------- */
int db_global_init(const char *host, const char *user,
                   const char *pass, const char *schema, unsigned port);

void db_global_end(void);

/* -------- 스레드 전용 초기화 / 종료 -------- */
int db_thread_init(void);
void db_thread_cleanup(void);

/* -------- 현재 스레드용 커넥션 핸들 -------- */
MYSQL *get_db(void);
