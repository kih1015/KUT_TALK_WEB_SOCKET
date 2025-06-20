#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
static const char *GUID =
    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/* HTTP 헤더 추출 */
static int extract_header(const char *req,
                          const char *key,
                          char *out, size_t cap) {
    const char *p = strcasestr(req, key);
    if (!p) return 0;
    p = strchr(p, ':'); if (!p) return 0;
    p++; while (*p == ' ') ++p;
    const char *e = strstr(p, "\r\n"); if (!e) return 0;
    size_t len = (e - p) < cap - 1 ? (size_t)(e - p) : cap - 1;
    strncpy(out, p, len); out[len] = '\0';
    return 1;
}

int websocket_handshake(int cli_fd) {
    char req[4096];
    ssize_t n;
    size_t total = 0;
    // 블로킹 모드에서 헤더 끝까지 수신
    while ((n = recv(cli_fd, req + total, sizeof(req) - 1 - total, 0)) > 0) {
        total += n;
        req[total] = '\0';
        if (strstr(req, "\r\n\r\n")) break;
    }
    if (n < 0) {
        perror("HS recv");
        return -1;
    }
    if (!strstr(req, "\r\n\r\n")) {
        // 헤더가 완전히 도착하지 않음
        return -1;
    }

    char key[128];
    if (!extract_header(req, "Sec-WebSocket-Key", key, sizeof(key))) {
        return -1;
    }

    // SHA-1(key + GUID)
    char concat[256];
    snprintf(concat, sizeof(concat), "%s%s", key, GUID);
    unsigned char sha1sum[SHA_DIGEST_LENGTH];
    SHA1((unsigned char *)concat, strlen(concat), sha1sum);

    // Base64 인코딩
    char accept_key[EVP_ENCODE_LENGTH(SHA_DIGEST_LENGTH) + 1];
    EVP_EncodeBlock((unsigned char *)accept_key, sha1sum, SHA_DIGEST_LENGTH);

    // 101 Switching Protocols 응답
    char res[512];
    int m = snprintf(res, sizeof(res),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept_key);
    if (m < 0 || m >= (int)sizeof(res)) return -1;

    // 디버깅용 로그
    printf("=== WebSocket Handshake Request ===\n%s\n", req);
    printf("=== WebSocket Handshake Response ===\n%.*s\n", m, res);

    ssize_t w = write(cli_fd, res, m);
    if (w != m) {
        perror("HS write");
        fflush("stdout");
    };

    return 0;
}
