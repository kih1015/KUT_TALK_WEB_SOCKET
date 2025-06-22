#pragma once

#include <stdint.h>
#include <time.h>

/* 채팅방 정보 (내 채팅방 목록, 공개 채팅방 목록) */
typedef struct {
    uint32_t room_id;
    char     title[81];
    char     room_type[8];
    uint32_t creator_id;
    time_t   created_at;
    uint32_t member_cnt;
    uint32_t unread_cnt;
} chat_room_t;

typedef struct {
    uint32_t message_id;
    uint32_t count;
} chat_unread_t;

/* 메시지 정보 */
typedef struct {
    uint32_t id;
    uint32_t room_id;
    uint32_t sender_id;
    char     sender_nick[64];
    char    *content;
    time_t   created_at;
    uint32_t unread_cnt;
} chat_message_t;


int chat_repo_find_public_rooms(
    chat_room_t **out_rooms, 
    size_t *out_count
);

/* ── 채팅방 참여/탈퇴 ── */
int chat_repo_join_room(uint32_t room_id, uint32_t user_id);
int chat_repo_leave_room(uint32_t room_id, uint32_t user_id);

/* ── 메시지 저장 & 조회 ── */
int chat_repo_save_message(
    uint32_t room_id,
    uint32_t sender_id,
    const char *content,
    uint32_t *out_message_id
);

int chat_repo_get_messages(
    uint32_t room_id,
    uint32_t page,
    uint32_t limit,
    chat_message_t **out_msgs,
    size_t *out_count
);

/* ── Unread 관리 ── */
int chat_repo_add_unread(uint32_t message_id, uint32_t user_id);
int chat_repo_clear_unread(uint32_t room_id, uint32_t user_id);
int chat_repo_count_unread(uint32_t room_id, uint32_t user_id, uint32_t *out_count);

/* ---------- 채팅방 멤버 조회 ---------- */
int chat_repo_get_room_members(
    uint32_t room_id,
    uint32_t **out_user_ids,
    size_t *out_count
);

/* ── 방의 모든 메시지별 언리드 카운트 ── */
int chat_repo_get_unread_counts(
    uint32_t room_id,
    chat_unread_t **out_array,
    size_t *out_count
);

/* ── 메시지별 전체 언리드 카운트 ── */
int chat_repo_count_message_unread(uint32_t message_id, uint32_t *out_count);
