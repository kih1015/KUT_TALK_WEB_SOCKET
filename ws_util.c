#include "ws_util.h"
#include <errno.h>

ssize_t readn(int fd, void *buf, size_t n) {
    size_t left = n; char *p = buf;
    while (left) {
        ssize_t r = read(fd, p, left);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) break;
        left -= r; p += r;
    }
    return n - left;
}

ssize_t writen(int fd, const void *buf, size_t n) {
    size_t left = n; const char *p = buf;
    while (left) {
        ssize_t r = write(fd, p, left);
        if (r <= 0) { if (errno == EINTR) continue; return -1; }
        left -= r; p += r;
    }
    return n;
}
