#include "chat_repository.h"

#include <stdio.h>

#include "db.h"
#include <mysql/mysql.h>
#include <stdlib.h>
#include <string.h>

/* ── 헬퍼: 결과 집합 끝까지 읽어 배열로 반환 ── */
static int fetch_rooms(MYSQL_RES *res, chat_room_t **out, size_t *cnt, int include_unread, uint32_t user_id) {
    size_t n = (size_t) mysql_num_rows(res);
    chat_room_t *arr = calloc(n, sizeof(chat_room_t));
    MYSQL_ROW row;
    size_t i = 0;

    while ((row = mysql_fetch_row(res))) {
        unsigned long *len = mysql_fetch_lengths(res);
        arr[i].room_id = (uint32_t) atoi(row[0]);
        strncpy(arr[i].title, row[1], sizeof arr[i].title - 1);
        strncpy(arr[i].room_type, row[2], sizeof arr[i].room_type - 1);
        arr[i].creator_id = (uint32_t) atoi(row[3]);
        arr[i].created_at = (time_t) atoi(row[4]);
        arr[i].member_cnt = (uint32_t) atoi(row[5]);
        if (include_unread && row[6]) {
            arr[i].unread_cnt = (uint32_t) atoi(row[6]);
        }
        i++;
    }
    mysql_free_result(res);
    *out = arr;
    *cnt = n;
    return 0;
}

/* ── 공개 채팅방 목록 ── */
int chat_repo_find_public_rooms(chat_room_t **out_rooms, size_t *out_count) {
    MYSQL *db = get_db();
    if (!db) return -1;

    const char *sql =
            "SELECT r.id, r.title, r.room_type, r.creator_id, "
            "       UNIX_TIMESTAMP(r.created_at), "
            "       (SELECT COUNT(*) FROM chat_room_member m2 WHERE m2.room_id=r.id),"
            "       0 /* no unread for public listing */ "
            "FROM chat_room r "
            "WHERE r.room_type='PUBLIC' "
            "ORDER BY r.created_at DESC";

    if (mysql_query(db, sql)) return -2;
    MYSQL_RES *res = mysql_store_result(db);
    return fetch_rooms(res, out_rooms, out_count, 0, 0);
}

/* ── 채팅방 참여 / 탈퇴 ── */
int chat_repo_join_room(uint32_t room_id, uint32_t user_id) {
    MYSQL *db = get_db();
    if (!db) return -1;

    MYSQL_STMT *st = mysql_stmt_init(db);
    const char *sql =
            "INSERT IGNORE INTO chat_room_member(room_id,user_id) VALUES(?,?)";
    mysql_stmt_prepare(st, sql, strlen(sql));
    MYSQL_BIND pb[2] = {{0}}, ub[2] = {{0}};
    pb[0].buffer_type = MYSQL_TYPE_LONG;
    pb[0].buffer = &room_id;
    pb[1].buffer_type = MYSQL_TYPE_LONG;
    pb[1].buffer = &user_id;
    mysql_stmt_bind_param(st, pb);
    if (mysql_stmt_execute(st)) {
        mysql_stmt_close(st);
        return -2;
    }
    mysql_stmt_close(st);
    return 0;
}

int chat_repo_leave_room(uint32_t room_id, uint32_t user_id) {
    MYSQL *db = get_db();
    if (!db) return -1;
    MYSQL_STMT *st = mysql_stmt_init(db);
    const char *sql =
            "DELETE FROM chat_room_member WHERE room_id=? AND user_id=?";
    mysql_stmt_prepare(st, sql, strlen(sql));
    MYSQL_BIND pb[2] = {{0}};
    pb[0].buffer_type = MYSQL_TYPE_LONG;
    pb[0].buffer = &room_id;
    pb[1].buffer_type = MYSQL_TYPE_LONG;
    pb[1].buffer = &user_id;
    mysql_stmt_bind_param(st, pb);
    if (mysql_stmt_execute(st)) {
        mysql_stmt_close(st);
        return -2;
    }
    mysql_stmt_close(st);
    return 0;
}

/* ── 메시지 저장 ── */
int chat_repo_save_message(uint32_t room_id, uint32_t sender_id,
                           const char *content, uint32_t *out_message_id) {
    MYSQL *db = get_db();
    if (!db) return -1;
    MYSQL_STMT *st = mysql_stmt_init(db);
    const char *sql =
            "INSERT INTO chat_message(room_id,sender_id,content) VALUES(?,?,?)";
    mysql_stmt_prepare(st, sql, strlen(sql));
    MYSQL_BIND pb[3] = {{0}};
    pb[0].buffer_type = MYSQL_TYPE_LONG;
    pb[0].buffer = &room_id;
    pb[1].buffer_type = MYSQL_TYPE_LONG;
    pb[1].buffer = &sender_id;
    pb[2].buffer_type = MYSQL_TYPE_STRING;
    pb[2].buffer = (char *) content;
    pb[2].buffer_length = strlen(content);
    mysql_stmt_bind_param(st, pb);
    if (mysql_stmt_execute(st)) {
        mysql_stmt_close(st);
        return -2;
    }
    *out_message_id = (uint32_t) mysql_stmt_insert_id(st);
    mysql_stmt_close(st);
    return 0;
}

/* ── Unread ── */
int chat_repo_add_unread(uint32_t message_id, uint32_t user_id) {
    MYSQL *db = get_db();
    if (!db) return -1;
    char sql[128];
    snprintf(sql, sizeof sql,
             "INSERT IGNORE INTO chat_message_unread(message_id,user_id) VALUES(%u,%u)",
             message_id, user_id);
    return mysql_query(db, sql) ? -2 : 0;
}

int chat_repo_clear_unread(uint32_t room_id, uint32_t user_id) {
    MYSQL *db = get_db();
    if (!db) return -1;
    char sql[256];
    snprintf(sql, sizeof sql,
             "DELETE u FROM chat_message_unread u "
             "JOIN chat_message m ON m.id=u.message_id "
             "WHERE m.room_id=%u AND u.user_id=%u",
             room_id, user_id);
    return mysql_query(db, sql) ? -2 : 0;
}

int chat_repo_count_unread(uint32_t room_id, uint32_t user_id, uint32_t *out_count) {
    MYSQL *db = get_db();
    if (!db) return -1;
    char sql[256];
    snprintf(sql, sizeof sql,
             "SELECT COUNT(*) FROM chat_message_unread u "
             "JOIN chat_message m ON m.id=u.message_id "
             "WHERE m.room_id=%u AND u.user_id=%u",
             room_id, user_id);
    if (mysql_query(db, sql)) return -2;
    MYSQL_RES *res = mysql_store_result(db);
    MYSQL_ROW row = mysql_fetch_row(res);
    *out_count = row ? (uint32_t) atoi(row[0]) : 0;
    mysql_free_result(res);
    return 0;
}

/* ---------- 채팅방 멤버 조회 구현 ---------- */
int chat_repo_get_room_members(uint32_t room_id,
                               uint32_t **out_user_ids,
                               size_t   *out_count)
{
    MYSQL *db = get_db();
    if (!db) return -1;

    MYSQL_STMT *st = mysql_stmt_init(db);
    const char *sql =
      "SELECT user_id FROM chat_room_member WHERE room_id = ?";
    mysql_stmt_prepare(st, sql, strlen(sql));

    // — 파라미터 바인딩
    MYSQL_BIND param = {0};
    param.buffer_type = MYSQL_TYPE_LONG;
    param.buffer      = &room_id;
    mysql_stmt_bind_param(st, &param);
    mysql_stmt_execute(st);

    // — 결과 버퍼링 & 행 수 확보
    mysql_stmt_store_result(st);
    size_t n = mysql_stmt_num_rows(st);
    uint32_t *ids = calloc(n, sizeof(uint32_t));

    // — 결과 바인딩 (임시 변수 사용)
    uint32_t tmp = 0;
    MYSQL_BIND result = {0};
    result.buffer_type   = MYSQL_TYPE_LONG;
    result.buffer        = &tmp;
    result.buffer_length = sizeof(tmp);
    result.is_null       = 0;
    result.length        = 0;
    mysql_stmt_bind_result(st, &result);

    // — fetch 루프
    size_t idx = 0;
    while (idx < n && mysql_stmt_fetch(st) == 0) {
        ids[idx++] = tmp;
    }

    mysql_stmt_close(st);

    *out_user_ids = ids;
    *out_count    = idx;  // 혹시 실제 읽은 행 수(idx)가 n보다 작으면 그 값으로…
    return 0;
}

/* ── 방의 모든 메시지별 언리드 카운트 ── */
int chat_repo_get_unread_counts(
    uint32_t room_id,
    chat_unread_t **out_array,
    size_t *out_count
) {
    MYSQL *db = get_db();
    if (!db) return -1;

    char sql[256];
    snprintf(sql, sizeof sql,
        "SELECT m.id, COUNT(u.user_id) "
        "FROM chat_message m "
        "LEFT JOIN chat_message_unread u ON u.message_id=m.id "
        "WHERE m.room_id=%u "
        "GROUP BY m.id",
        room_id);

    if (mysql_query(db, sql)) return -2;
    MYSQL_RES *res = mysql_store_result(db);
    size_t n = mysql_num_rows(res);

    chat_unread_t *arr = calloc(n, sizeof(chat_unread_t));
    MYSQL_ROW row;
    size_t i = 0;
    while ((row = mysql_fetch_row(res))) {
        arr[i].message_id = (uint32_t)atoi(row[0]);
        arr[i].count      = (uint32_t)atoi(row[1]);
        i++;
    }
    mysql_free_result(res);
    *out_array  = arr;
    *out_count  = n;
    return 0;
}

/* ── 메시지별 전체 언리드 카운트 ── */
int chat_repo_count_message_unread(uint32_t message_id, uint32_t *out_count) {
    MYSQL *db = get_db();
    if (!db) return -1;

    char sql[128];
    snprintf(sql, sizeof sql,
        "SELECT COUNT(*) FROM chat_message_unread WHERE message_id=%u",
        message_id);

    if (mysql_query(db, sql)) return -2;
    MYSQL_RES *res = mysql_store_result(db);
    MYSQL_ROW row = mysql_fetch_row(res);
    *out_count = row ? (uint32_t)atoi(row[0]) : 0;
    mysql_free_result(res);
    return 0;
}
