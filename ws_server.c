#include "ws_handshake.h"
#include "ws_frame.h"
#include "ws_util.h"
#include "db.h"

#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 클라이언트 정보 전달용 */
typedef struct {
    int fd;
} client_arg_t;

static int make_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int tcp_listen(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };
    bind(fd, (struct sockaddr *) &addr, sizeof(addr));
    listen(fd, SOMAXCONN);
    return fd;
}

/* 클라이언트 처리 스레드 */
static void *client_thread(void *arg) {
    client_arg_t *carg = arg;
    int fd = carg->fd;
    free(carg);

    /* ─── 이 스레드 전용 DB 커넥션 초기화 ─── */
    if (db_thread_init() != 0) {
        fprintf(stderr, "Failed to init DB for thread\n");
        close(fd);
        return NULL;
    }

    /* ─── WebSocket 핸드셰이크 ─── */
    if (websocket_handshake(fd) != 0) {
        close(fd);
        db_thread_cleanup();
        return NULL;
    }
    make_nonblock(fd);

    /* ─── 메인 루프: 프레임 읽고 에코 ─── */
    while (1) {
        ws_frame_t f;
        int ret = ws_recv(fd, &f);
        if (ret <= 0) break; // 클라이언트 종료 or 에러

        if (f.opcode == 0x8) {
            // Close
            free(f.payload);
            break;
        }

        /* --- 예시: DB 사용 --- */
        MYSQL *db = get_db();
        /* ex) mysql_query(db, "INSERT INTO logs (msg) VALUES (...)"); */

        /* --- 에코 응답 --- */
        uint8_t buf[2048];
        size_t n = ws_build_text_frame(f.payload, f.len, buf);
        writen(fd, buf, n);
        free(f.payload);
    }

    /* ─── 정리 ─── */
    close(fd);
    db_thread_cleanup();
    return NULL;
}

int main(int argc, char **argv) {
    const char *db_user = getenv("DB_USER");
    const char *db_pass = getenv("DB_PASS");
    /* ─── 글로벌 DB 설정 ─── */
    if (db_global_init(
            "127.0.0.1", db_user, db_pass,
            "kuttalk_db", 3306) != 0) {
        fprintf(stderr, "DB init failed\n");
        return 1;
    }

    int PORT = 8090;
    /* ─── TCP 리스닝 ─── */
    int lfd = tcp_listen(PORT);
    printf("Listening on :%d\n", PORT);

    while (1) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) continue;

        /* 스레드 인자 준비 */
        client_arg_t *carg = calloc(1, sizeof(*carg));
        carg->fd = cfd;

        /* 스레드 생성 (detached) */
        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&tid, &attr, client_thread, carg);
        pthread_attr_destroy(&attr);
    }
}
