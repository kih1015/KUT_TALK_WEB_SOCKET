#include "ws_handshake.h"
#include "ws_frame.h"
#include "ws_util.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define PORT 8090
#define MAX_EVENTS 1024

/* ── 클라이언트 구조 ── */
typedef struct client {
    int fd;
    int handshaked;
} client_t;

/* ── 유틸 ── */
static int make_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
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

/* ── 클라이언트 처리 ── */
static void handle_client(int ep, client_t *cli) {
    int fd = cli->fd;

    /* 1) 핸드셰이크 */
    if (!cli->handshaked) {
        if (websocket_handshake(fd) == 0) {
            make_nonblock(fd);
            cli->handshaked = 1;
        } else {
            /* 실패 */
            close(fd);
            free(cli);
        }
        return;
    }

    /* 2) 프레임 */
    ws_frame_t f;
    if (ws_recv(fd, &f) < 0) {
        /* EOF / 오류 */
        close(fd);
        free(cli);
        return;
    }

    if (f.opcode == 0x8) {
        /* Close */
        free(f.payload);
        close(fd);
        free(cli);
        return;
    }

    /* Echo */
    uint8_t frame[2048];
    size_t flen = ws_build_text_frame(f.payload, f.len, frame);
    writen(fd, frame, flen);
    free(f.payload);
}

int main() {
    int lfd = tcp_listen(PORT);
    int ep = epoll_create1(0);

    struct epoll_event ev = {.events = EPOLLIN, .data.fd = lfd};
    epoll_ctl(ep, EPOLL_CTL_ADD, lfd, &ev);

    struct epoll_event events[MAX_EVENTS];
    printf("Listening on :%d\n", PORT);

    while (1) {
        int n = epoll_wait(ep, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; ++i) {
            /* 새 연결? */
            if (events[i].data.fd == lfd) {
                int cfd = accept(lfd, NULL, NULL);
                client_t *cli = calloc(1, sizeof(client_t));
                cli->fd = cfd;
                ev.events = EPOLLIN;
                ev.data.ptr = cli;
                epoll_ctl(ep, EPOLL_CTL_ADD, cfd, &ev);
            } else {
                client_t *cli = events[i].data.ptr;
                handle_client(ep, cli);
            }
        }
    }
}
