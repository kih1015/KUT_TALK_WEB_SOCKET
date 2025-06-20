#include <mysql.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "db.h"
/*------------------------------------------------------------------*/
/* 세션 조회                                                         */
/*------------------------------------------------------------------*/
int session_repository_find_id(
    const char *sid,
    uint32_t *out_user_id,
    time_t *out_exp
) {
    MYSQL *db = get_db();
    if (!db) return -1;

    MYSQL_STMT *st = mysql_stmt_init(db);
    const char *sql =
            "SELECT userid, UNIX_TIMESTAMP(expires_at) "
            "FROM sessions WHERE id = ? LIMIT 1";

    if (mysql_stmt_prepare(st, sql, strlen(sql))) {
        mysql_stmt_close(st);
        return -2;
    }

    /* 파라미터 바인딩 (sid) */
    MYSQL_BIND pb = {0};
    pb.buffer_type = MYSQL_TYPE_STRING;
    pb.buffer = (char *) sid;
    pb.buffer_length = 64;
    mysql_stmt_bind_param(st, &pb);

    if (mysql_stmt_execute(st)) {
        mysql_stmt_close(st);
        return -3;
    }

    /* 결과 바인딩 */
    uint32_t uid_val = 0;
    time_t exp_val = 0;

    MYSQL_BIND rb[2] = {0};
    rb[0].buffer_type = MYSQL_TYPE_LONG;
    rb[0].buffer = &uid_val;
    rb[0].is_unsigned = 1;

    rb[1].buffer_type = MYSQL_TYPE_LONGLONG;
    rb[1].buffer = &exp_val;

    mysql_stmt_bind_result(st, rb);

    int fs = mysql_stmt_fetch(st);
    mysql_stmt_close(st);

    if (fs == MYSQL_NO_DATA) return 1; /* 세션 없음 */
    if (fs) return -4; /* fetch 오류 */

    if (out_user_id) *out_user_id = uid_val;
    if (out_exp) *out_exp = exp_val;
    return 0; /* 성공 */
}

char *session_repository_get_nick(uint32_t user_id) {
    MYSQL *db = get_db();
    if (!db) return NULL;

    MYSQL_STMT *st = mysql_stmt_init(db);
    const char *sql = "SELECT nickname FROM users WHERE id = ? LIMIT 1";
    if (mysql_stmt_prepare(st, sql, strlen(sql)) != 0) {
        mysql_stmt_close(st);
        return NULL;
    }

    /* 파라미터 바인딩 */
    MYSQL_BIND pb = {0};
    pb.buffer_type = MYSQL_TYPE_LONG;
    pb.buffer = &user_id;
    if (mysql_stmt_bind_param(st, &pb) != 0) {
        mysql_stmt_close(st);
        return NULL;
    }

    if (mysql_stmt_execute(st) != 0) {
        mysql_stmt_close(st);
        return NULL;
    }

    /* 결과 메타·바인딩 */
    MYSQL_RES *meta = mysql_stmt_result_metadata(st);
    if (!meta) {
        mysql_stmt_close(st);
        return NULL;
    }
    MYSQL_BIND rb = {0};
    /* nickname 최대 64자 가정 */
    char nickbuf[64] = {0};
    unsigned long nicklen = 0;
    rb.buffer_type = MYSQL_TYPE_STRING;
    rb.buffer = nickbuf;
    rb.buffer_length = sizeof(nickbuf) - 1;
    rb.length = &nicklen;
    if (mysql_stmt_bind_result(st, &rb) != 0) {
        mysql_free_result(meta);
        mysql_stmt_close(st);
        return NULL;
    }

    mysql_stmt_store_result(st);
    char *result = NULL;
    if (mysql_stmt_fetch(st) == 0 && nicklen > 0) {
        /* strdup 으로 힙에 복사 */
        result = malloc(nicklen + 1);
        memcpy(result, nickbuf, nicklen);
        result[nicklen] = '\0';
    }

    mysql_free_result(meta);
    mysql_stmt_close(st);
    return result;
}
