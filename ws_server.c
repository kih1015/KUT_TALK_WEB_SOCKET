// ws_server.c

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>

#include <cjson/cJSON.h>

#include "ws_handshake.h"
#include "ws_frame.h"
#include "ws_util.h"
#include "session_repository.h"
#include "chat_repository.h"
#include "db.h"

#define PORT          8090
#define MAX_EVENTS    1024
#define PING_INTERVAL 3    // seconds
#define PONG_TIMEOUT  3    // seconds

typedef struct client {
    int            fd;
    int            handshaked;
    uint32_t       user_id;
    int            room_id;
    time_t         last_pong;
    struct client *next;
} client_t;

static client_t *clients = NULL;
static pthread_mutex_t clients_mtx = PTHREAD_MUTEX_INITIALIZER;

// epoll fd 전역 저장
static int epoll_fd = -1;

// -------------------------------------------------------
// 논블로킹 소켓 생성
static int make_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// TCP 리스닝 소켓 생성
static int tcp_listen(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };
    bind(fd, (struct sockaddr*)&addr, sizeof addr);
    listen(fd, SOMAXCONN);
    return fd;
}

// clients 리스트에서 cli 제거
static void remove_client(client_t *cli) {
    pthread_mutex_lock(&clients_mtx);
    client_t **p = &clients;
    while (*p && *p != cli) p = &(*p)->next;
    if (*p) *p = cli->next;
    pthread_mutex_unlock(&clients_mtx);
}

// -------------------------------------------------------
// 완전한 연결 해제: epoll, 소켓 닫기, 리스트 제거, 메모리 해제
static void disconnect_client(client_t *cli) {
    // 1) epoll에서 제거
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cli->fd, NULL);
    // 2) 소켓 닫기
    close(cli->fd);
    // 3) 내부 리스트에서 제거
    remove_client(cli);
    // 4) 메모리 해제
    free(cli);
}

// -------------------------------------------------------
// JSON 전송 헬퍼
static void send_json(client_t *cli, cJSON *msg) {
    char *text = cJSON_PrintUnformatted(msg);
    size_t len  = strlen(text);
    uint8_t *frame = malloc(len + 16);
    size_t flen    = ws_build_text_frame((uint8_t*)text, len, frame);
    writen(cli->fd, frame, flen);
    free(frame);
    free(text);
    cJSON_Delete(msg);
}

// 방 단위 브로드캐스트
static void broadcast_room(int room, cJSON *msg) {
    char *text = cJSON_PrintUnformatted(msg);
    size_t len  = strlen(text);
    uint8_t *frame = malloc(len + 16);
    size_t flen    = ws_build_text_frame((uint8_t*)text, len, frame);
    free(text);
    cJSON_Delete(msg);

    pthread_mutex_lock(&clients_mtx);
    for (client_t *c = clients; c; c = c->next) {
        if (c->handshaked && c->room_id == room) {
            writen(c->fd, frame, flen);
        }
    }
    pthread_mutex_unlock(&clients_mtx);
    free(frame);
}

// 전체 브로드캐스트
static void broadcast_all(cJSON *msg) {
    char *text = cJSON_PrintUnformatted(msg);
    size_t len = strlen(text);
    uint8_t *frame = malloc(len + 16);
    size_t flen  = ws_build_text_frame((uint8_t*)text, len, frame);
    free(text);
    cJSON_Delete(msg);

    pthread_mutex_lock(&clients_mtx);
    for (client_t *c = clients; c; c = c->next) {
        if (c->handshaked) {
            writen(c->fd, frame, flen);
        }
    }
    pthread_mutex_unlock(&clients_mtx);
    free(frame);
}

// Unread 알림
static void notify_unread(uint32_t room, uint32_t msg_id, uint32_t sender) {
    pthread_mutex_lock(&clients_mtx);
    for (client_t *c = clients; c; c = c->next) {
        if (!c->handshaked)                continue;
        if ((uint32_t)c->user_id == sender) continue;
        if (c->room_id == (int)room)       continue;

        if (chat_repo_add_unread(msg_id, c->user_id) != 0) {
            fprintf(stderr, "ERROR: chat_repo_add_unread failed msg=%u uid=%u\n", msg_id, c->user_id);
        }

        uint32_t ucnt = 0;
        if (chat_repo_count_unread(room, c->user_id, &ucnt) != 0) {
            fprintf(stderr, "ERROR: chat_repo_count_unread failed room=%u uid=%u\n", room, c->user_id);
        }

        cJSON *n = cJSON_CreateObject();
        cJSON_AddStringToObject(n, "type",  "unread");
        cJSON_AddNumberToObject(n, "room",  room);
        cJSON_AddNumberToObject(n, "count", ucnt);
        send_json(c, n);
    }
    pthread_mutex_unlock(&clients_mtx);
}

// -------------------------------------------------------
static void handle_client(client_t *cli) {
    int fd = cli->fd;

    // 1) WebSocket 핸드셰이크
    if (!cli->handshaked) {
        if (websocket_handshake(fd) == 0) {
            make_nonblock(fd);
            cli->handshaked = 1;
            cli->user_id    = 0;
            cli->room_id    = 0;
            cli->last_pong  = time(NULL);
        } else {
            disconnect_client(cli);
        }
        return;
    }

    // 2) 프레임 수신
    ws_frame_t f;
    if (ws_recv(fd, &f) < 0) {
        disconnect_client(cli);
        return;
    }

    // close opcode
    if (f.opcode == 0x8) {
        free(f.payload);
        disconnect_client(cli);
        return;
    }

    // 3) JSON 파싱
    cJSON *req = cJSON_ParseWithLength((char*)f.payload, f.len);
    if (req) {
        cli->last_pong = time(NULL);

        cJSON *jt = cJSON_GetObjectItem(req, "type");
        if (cJSON_IsString(jt)) {
            // pong
            if (strcmp(jt->valuestring, "pong") == 0) {
                cJSON_Delete(req);
                free(f.payload);
                return;
            }
            // auth
            else if (strcmp(jt->valuestring, "auth") == 0) {
                const char *sid = cJSON_GetObjectItem(req, "sid")->valuestring;
                uint32_t uid; time_t exp;
                if (session_repository_find_id(sid, &uid, &exp) == 0) {
                    cli->user_id = uid;
                    cJSON *ok = cJSON_CreateObject();
                    cJSON_AddStringToObject(ok, "type", "auth_ok");
                    send_json(cli, ok);
                }
                cJSON_Delete(req);
                free(f.payload);
                return;
            }
            // join
            else if (strcmp(jt->valuestring, "join") == 0) {
                const char *sid   = cJSON_GetObjectItem(req, "sid")->valuestring;
                int          room = cJSON_GetObjectItem(req, "room")->valueint;
                uint32_t     uid; time_t exp;
                if (session_repository_find_id(sid, &uid, &exp) == 0) {
                    // unread 리스트 조회
                    chat_unread_t *old; size_t old_cnt;
                    if (chat_repo_get_unread_counts_for_user(room, uid, &old, &old_cnt) != 0) {
                        old_cnt = 0;
                    }
                    uint32_t *msg_ids = NULL;
                    if (old_cnt > 0) {
                        msg_ids = malloc(old_cnt * sizeof(uint32_t));
                        for (size_t i = 0; i < old_cnt; i++) {
                            msg_ids[i] = old[i].message_id;
                        }
                    }
                    free(old);

                    chat_repo_clear_unread(room, uid);

                    // 클라이언트에게 count=0 전송
                    {
                        cJSON *clear = cJSON_CreateObject();
                        cJSON_AddStringToObject(clear, "type",  "unread");
                        cJSON_AddNumberToObject(clear, "room",  room);
                        cJSON_AddNumberToObject(clear, "count", 0);
                        send_json(cli, clear);
                    }

                    // 내부 상태 업데이트
                    cli->user_id = uid;
                    cli->room_id = room;

                    // joined 브로드캐스트
                    {
                        uint32_t *members; size_t mcnt;
                        if (chat_repo_get_room_members(room, &members, &mcnt) == 0) {
                            cJSON *res = cJSON_CreateObject();
                            cJSON_AddStringToObject(res, "type", "joined");
                            cJSON_AddNumberToObject(res, "room", room);
                            cJSON *ua = cJSON_AddArrayToObject(res, "users");
                            for (size_t i = 0; i < mcnt; i++) {
                                cJSON_AddItemToArray(ua, cJSON_CreateNumber(members[i]));
                            }
                            free(members);
                            broadcast_room(room, res);
                        }
                    }

                    // 이전 unread 메시지별 updated-message 전송
                    if (msg_ids) {
                        for (size_t i = 0; i < old_cnt; i++) {
                            uint32_t mid = msg_ids[i];
                            int new_cnt = chat_repo_get_unread_count_for_message(room, mid);
                            cJSON *upd = cJSON_CreateObject();
                            cJSON_AddStringToObject(upd, "type",       "updated-message");
                            cJSON_AddNumberToObject(upd, "id",         mid);
                            cJSON_AddNumberToObject(upd, "unread_cnt", new_cnt);
                            broadcast_room(room, upd);
                        }
                        free(msg_ids);
                    }
                }
            }
            // leave
            else if (strcmp(jt->valuestring, "leave") == 0) {
                uint32_t rid = cli->room_id;
                cli->room_id = 0;
                cJSON *res = cJSON_CreateObject();
                cJSON_AddStringToObject(res, "type", "left");
                cJSON_AddNumberToObject(res, "room", rid);
                cJSON_AddNumberToObject(res, "user", cli->user_id);
                broadcast_room(rid, res);
            }
            // message
            else if (strcmp(jt->valuestring, "message") == 0) {
                const char *ct = cJSON_GetObjectItem(req, "content")->valuestring;
                uint32_t mid = 0;

                if (chat_repo_save_message(cli->room_id, cli->user_id, ct, &mid) != 0) {
                    fprintf(stderr, "ERROR: chat_repo_save_message failed\n");
                    cJSON_Delete(req);
                    free(f.payload);
                    return;
                }

                // 같은 방에 접속 안 한 멤버에게만 unread 추가
                {
                    uint32_t *members; size_t mcnt;
                    if (chat_repo_get_room_members(cli->room_id, &members, &mcnt) == 0) {
                        bool *online = calloc(mcnt, sizeof(bool));
                        pthread_mutex_lock(&clients_mtx);
                        for (client_t *c = clients; c; c = c->next) {
                            if (c->handshaked && c->room_id == cli->room_id) {
                                for (size_t j = 0; j < mcnt; j++) {
                                    if (members[j] == c->user_id) {
                                        online[j] = true;
                                        break;
                                    }
                                }
                            }
                        }
                        pthread_mutex_unlock(&clients_mtx);
                        for (size_t i = 0; i < mcnt; i++) {
                            if (members[i] == cli->user_id) continue;
                            if (online[i])            continue;
                            chat_repo_add_unread(mid, members[i]);
                        }
                        free(online);
                        free(members);
                    }
                }

                notify_unread(cli->room_id, mid, cli->user_id);

                uint32_t unread_cnt = 0;
                chat_repo_count_message_unread(mid, &unread_cnt);

                cJSON *res = cJSON_CreateObject();
                cJSON_AddStringToObject(res, "type",       "message");
                cJSON_AddNumberToObject(res, "room",       cli->room_id);
                cJSON_AddNumberToObject(res, "id",         mid);
                cJSON_AddNumberToObject(res, "sender",     cli->user_id);
                {
                    char *nick = session_repository_get_nick(cli->user_id);
                    cJSON_AddStringToObject(res, "nick", nick);
                    free(nick);
                }
                cJSON_AddStringToObject(res, "content",    ct);
                cJSON_AddNumberToObject(res, "ts",         time(NULL));
                cJSON_AddNumberToObject(res, "unread_cnt", unread_cnt);
                broadcast_room(cli->room_id, res);
            }
            // update-chat-room
            else if (strcmp(jt->valuestring, "update-chat-room") == 0) {
                cJSON *res = cJSON_CreateObject();
                cJSON_AddStringToObject(res, "type", "updated-chat-room");
                broadcast_all(res);
            }
        }
        cJSON_Delete(req);
        free(f.payload);
        return;
    }

    // JSON 아니면 echo
    {
        uint8_t buf[2048];
        size_t bl = ws_build_text_frame(f.payload, f.len, buf);
        writen(fd, buf, bl);
    }
    free(f.payload);
    return;
}

int main() {
    // DB 초기화
    const char *db_user = getenv("DB_USER");
    const char *db_pass = getenv("DB_PASS");
    if (!db_user || !db_pass) {
        fprintf(stderr, "ERROR: DB_USER and DB_PASS must be set\n");
        return EXIT_FAILURE;
    }
    if (db_global_init("127.0.0.1", db_user, db_pass, "kuttalk_db", 3306) != 0) {
        fprintf(stderr, "ERROR: db_global_init failed\n");
        return EXIT_FAILURE;
    }
    if (db_thread_init() != 0) {
        fprintf(stderr, "ERROR: db_thread_init failed\n");
        db_global_end();
        return EXIT_FAILURE;
    }

    // listen + epoll
    int lfd = tcp_listen(PORT);
    epoll_fd = epoll_create1(0);
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = lfd };
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, lfd, &ev);

    printf("Listening on :%d\n", PORT);

    struct epoll_event events[MAX_EVENTS];
    time_t last_ping = time(NULL);

    while (1) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if (n < 0 && errno == EINTR) continue;

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == lfd) {
                int cfd = accept(lfd, NULL, NULL);
                make_nonblock(cfd);
                client_t *cli = calloc(1, sizeof(*cli));
                cli->fd         = cfd;
                cli->handshaked = 0;
                cli->last_pong  = time(NULL);
                pthread_mutex_lock(&clients_mtx);
                cli->next = clients;
                clients   = cli;
                pthread_mutex_unlock(&clients_mtx);
                struct epoll_event cev = { .events = EPOLLIN, .data.ptr = cli };
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cfd, &cev);
            } else {
                client_t *cli = events[i].data.ptr;
                handle_client(cli);
            }
        }

        time_t now = time(NULL);

        // 1) app-level ping 전송
        if (now - last_ping >= PING_INTERVAL) {
            pthread_mutex_lock(&clients_mtx);
            for (client_t *c = clients; c; c = c->next) {
                if (c->handshaked) {
                    cJSON *ping = cJSON_CreateObject();
                    cJSON_AddStringToObject(ping, "type", "ping");
                    send_json(c, ping);
                }
            }
            pthread_mutex_unlock(&clients_mtx);
            last_ping = now;
        }

        // 2) app-level pong 타임아웃 검사
        pthread_mutex_lock(&clients_mtx);
        client_t *prev = NULL, *c = clients;
        while (c) {
            client_t *next = c->next;
            if (c->handshaked && (now - c->last_pong) > PONG_TIMEOUT) {
                disconnect_client(c);
            } else {
                prev = c;
            }
            c = next;
        }
        pthread_mutex_unlock(&clients_mtx);
    }

    db_thread_cleanup();
    db_global_end();
    return EXIT_SUCCESS;
}
