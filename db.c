#include "db.h"
#include <stdio.h>
#include <string.h>

/* TLS 커넥션 포인터 */
static __thread MYSQL *tls_db = NULL;

/* 앱 전체 공용 설정 */
static struct {
    char host[64], user[64], pass[64], schema[64];
    unsigned port;
} g_cfg;

int db_global_init(const char *h, const char *u,
                   const char *p, const char *s, unsigned port) {
    mysql_library_init(0, NULL, NULL); /* 글로벌 초기화 */

    strncpy(g_cfg.host, h, sizeof g_cfg.host - 1);
    strncpy(g_cfg.user, u, sizeof g_cfg.user - 1);
    strncpy(g_cfg.pass, p, sizeof g_cfg.pass - 1);
    strncpy(g_cfg.schema, s, sizeof g_cfg.schema - 1);
    g_cfg.port = port;
    return 0;
}

void db_global_end(void) {
    mysql_library_end();
}

/* ---------- TLS 커넥션 초기화 ---------- */
int db_thread_init(void) {
    if (tls_db) return 0; /* 이미 열린 경우 */

    /* 스레드 전용 libmysql 초기화 */
    mysql_thread_init();

    tls_db = mysql_init(NULL);
    if (!tls_db) return -1;

    if (!mysql_real_connect(tls_db,
                            g_cfg.host, g_cfg.user, g_cfg.pass,
                            g_cfg.schema, g_cfg.port, NULL, CLIENT_MULTI_STATEMENTS)) {
        fprintf(stderr, "DB connect error: %s\n",
                mysql_error(tls_db));
        mysql_close(tls_db);
        tls_db = NULL;
        mysql_thread_end();
        return -2;
                            }
    /* 필요하면 SET NAMES utf8mb4; 등 실행 */
    return 0;
}

/* ---------- TLS 커넥션 종료 ---------- */
void db_thread_cleanup(void) {
    if (tls_db) {
        mysql_close(tls_db);
        tls_db = NULL;
    }
    mysql_thread_end();
}

/* ---------- Getter ---------- */
MYSQL *get_db(void) { return tls_db; }
