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

#define PORT         8090
#define MAX_EVENTS   1024
#define PING_INTERVAL 3   // seconds
#define PONG_TIMEOUT  3   // seconds

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

// 논블로킹 소켓
static int make_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// TCP listening socket
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

// 클라이언트 제거
static void remove_client(client_t *cli) {
    pthread_mutex_lock(&clients_mtx);
    client_t **p = &clients;
    while (*p && *p != cli) p = &(*p)->next;
    if (*p) *p = cli->next;
    pthread_mutex_unlock(&clients_mtx);
}

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

// 방 브로드캐스트
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

// Unread 알림
static void notify_unread(uint32_t room, uint32_t msg_id, uint32_t sender) {
    pthread_mutex_lock(&clients_mtx);
    for (client_t *c = clients; c; c = c->next) {
        // 1) 핸드셰이크 완료 & 본인 아님 & 다른 방에 접속 중인 경우만
        if (!c->handshaked)                continue;
        if ((uint32_t)c->user_id == sender) continue;
        if (c->room_id == (int)room)       continue;

        // 2) DB에 unread 추가
        if (chat_repo_add_unread(msg_id, c->user_id) != 0) {
            fprintf(stderr,
                "ERROR: chat_repo_add_unread failed msg=%u uid=%u\n",
                msg_id, c->user_id
            );
        }

        // 3) 남은 언리드 개수 조회
        uint32_t ucnt = 0;
        if (chat_repo_count_unread(room, c->user_id, &ucnt) != 0) {
            fprintf(stderr,
                "ERROR: chat_repo_count_unread failed room=%u uid=%u\n",
                room, c->user_id
            );
        }

        // 4) JSON 알림 전송
        cJSON *n = cJSON_CreateObject();
        cJSON_AddStringToObject(n, "type",  "unread");
        cJSON_AddNumberToObject(n, "room",  room);
        cJSON_AddNumberToObject(n, "count", ucnt);
        send_json(c, n);
    }
    pthread_mutex_unlock(&clients_mtx);
}

static void handle_client(client_t *cli) {
    int fd = cli->fd;

    /* 1) 핸드셰이크 */
    if (!cli->handshaked) {
        if (websocket_handshake(fd) == 0) {
            make_nonblock(fd);
            cli->handshaked = 1;
            cli->user_id    = 0;
            cli->room_id    = 0;
            cli->last_pong  = time(NULL);
        } else {
            close(fd);
            remove_client(cli);
            free(cli);
        }
        return;
    }

    /* 2) 프레임 수신 */
    ws_frame_t f;
    if (ws_recv(fd, &f) < 0) {
        goto disconnect;
    }

    /* Close 코드 */
    if (f.opcode == 0x8) {
        free(f.payload);
        goto disconnect;
    }

    /* 3) JSON 파싱 */
    cJSON *req = cJSON_ParseWithLength((char*)f.payload, f.len);
    if (req) {
        // ——— JSON 메시지 받았으니 pong 대신 last_pong 갱신 ———
        cli->last_pong = time(NULL);

        cJSON *jt = cJSON_GetObjectItem(req, "type");
        if (cJSON_IsString(jt)) {
            // application‑level pong 처리
            if (strcmp(jt->valuestring, "pong") == 0) {
                cJSON_Delete(req);
                free(f.payload);
                return;
            }
            // join 처리
			if (strcmp(jt->valuestring, "join") == 0) {
    			const char *sid = cJSON_GetObjectItem(req, "sid")->valuestring;
    			int room        = cJSON_GetObjectItem(req, "room")->valueint;
    			uint32_t uid; time_t exp;
    			if (session_repository_find_id(sid, &uid, &exp) == 0) {
        			// 1) 해당 방의 unread 모두 지우기
        			chat_repo_clear_unread(room, uid);

        			// 2) 클라이언트에게 unread=0 알림
        			cJSON *clear = cJSON_CreateObject();
        			cJSON_AddStringToObject(clear, "type",  "unread");
        			cJSON_AddNumberToObject(clear, "room",  room);
        			cJSON_AddNumberToObject(clear, "count", 0);
        			send_json(cli, clear);

        			// 3) 내부 상태 업데이트
        			cli->user_id = uid;
        			cli->room_id = room;

        			// 4) 다른 멤버들에게 joined 브로드캐스트
        			uint32_t *m; size_t mcnt;
        			chat_repo_get_room_members(room, &m, &mcnt);
        			cJSON *res = cJSON_CreateObject();
        			cJSON_AddStringToObject(res, "type", "joined");
        			cJSON_AddNumberToObject(res, "room", room);
        			cJSON *ua = cJSON_AddArrayToObject(res, "users");
        			for (size_t i = 0; i < mcnt; i++) {
            			cJSON_AddItemToArray(ua, cJSON_CreateNumber(m[i]));
        			}
        			free(m);
        			broadcast_room(room, res);
    			}
			}
            // leave 처리
            else if (strcmp(jt->valuestring, "leave") == 0) {
                uint32_t rid = cli->room_id;
                cli->room_id = 0;
                cJSON *res = cJSON_CreateObject();
                cJSON_AddStringToObject(res, "type", "left");
                cJSON_AddNumberToObject(res, "room", rid);
                cJSON_AddNumberToObject(res, "user", cli->user_id);
                broadcast_room(rid, res);
            }
            // message 처리
            else if (strcmp(jt->valuestring, "message") == 0) {
                const char *ct = cJSON_GetObjectItem(req, "content")->valuestring;
                uint32_t mid;
                chat_repo_save_message(cli->room_id, cli->user_id, ct, &mid);
                cJSON *res = cJSON_CreateObject();
                cJSON_AddStringToObject(res, "type", "message");
                cJSON_AddNumberToObject(res, "room", cli->room_id);
                cJSON_AddNumberToObject(res, "id", mid);
                cJSON_AddNumberToObject(res, "sender", cli->user_id);
                char *nick = session_repository_get_nick(cli->user_id);
                cJSON_AddStringToObject(res, "nick", nick);
                free(nick);
                cJSON_AddStringToObject(res, "content", ct);
                cJSON_AddNumberToObject(res, "ts", time(NULL));
                broadcast_room(cli->room_id, res);
                notify_unread(cli->room_id, mid, cli->user_id);
            }
        }
        cJSON_Delete(req);
        free(f.payload);
        return;
    }

    /* JSON 아니면 echo */
    {
        uint8_t buf[2048];
        size_t bl = ws_build_text_frame(f.payload, f.len, buf);
        writen(fd, buf, bl);
    }
    free(f.payload);
    return;

disconnect:
    close(fd);
    remove_client(cli);
    free(cli);
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
    int ep  = epoll_create1(0);
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = lfd };
    epoll_ctl(ep, EPOLL_CTL_ADD, lfd, &ev);

    printf("Listening on :%d\n", PORT);

    struct epoll_event events[MAX_EVENTS];
    time_t last_ping = time(NULL);

    while (1) {
        int n = epoll_wait(ep, events, MAX_EVENTS, 1000);
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
                epoll_ctl(ep, EPOLL_CTL_ADD, cfd, &cev);
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
                epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL);
                close(c->fd);
                if (prev) prev->next = next;
                else clients       = next;
                free(c);
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
