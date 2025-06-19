#include "ws_handshake.h"
#include "ws_util.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <string.h>

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
    strncpy(out, p, len); out[len] = 0;
    return 1;
}

int websocket_handshake(int cli_fd) {
    char req[2048], key[128];
    ssize_t n = read(cli_fd, req, sizeof(req) - 1);
    if (n <= 0) return -1;
    req[n] = 0;

    if (!extract_header(req, "Sec-WebSocket-Key", key, sizeof(key)))
        return -1;

    /* SHA‑1(key + GUID) */
    char concat[160];
    snprintf(concat, sizeof(concat), "%s%s", key, GUID);
    unsigned char sha1[SHA_DIGEST_LENGTH];
    SHA1((unsigned char *)concat, strlen(concat), sha1);

    /* Base64 – EVP_EncodeBlock */
    char accept_key[EVP_ENCODE_LENGTH(SHA_DIGEST_LENGTH)+1];
    EVP_EncodeBlock((unsigned char *)accept_key, sha1, SHA_DIGEST_LENGTH);

    /* 101 Switching Protocols 응답 */
    dprintf(cli_fd,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept_key);

    return 0;
}
