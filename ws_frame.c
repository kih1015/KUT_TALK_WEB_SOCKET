#include "ws_frame.h"
#include "ws_util.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <endian.h>

/* ---------- 수신 ---------- */
int ws_recv(int fd, ws_frame_t *o) {
    uint8_t hdr[2];
    if (readn(fd, hdr, 2) != 2) return -1;

    o->fin = hdr[0] & 0x80;
    o->opcode = hdr[0] & 0x0F;
    int mask = hdr[1] & 0x80;
    uint64_t len = hdr[1] & 0x7F;

    if (len == 126) {
        uint16_t l16;
        readn(fd, &l16, 2);
        len = ntohs(l16);
    } else if (len == 127) {
        uint64_t l64;
        readn(fd, &l64, 8);
        len = be64toh(l64);
    }
    uint8_t mkey[4];
    if (mask) readn(fd, mkey, 4);

    o->payload = malloc(len);
    if (!o->payload) return -1;
    if (readn(fd, o->payload, len) != (ssize_t) len) return -1;

    if (mask)
        for (uint64_t i = 0; i < len; ++i)
            o->payload[i] ^= mkey[i & 3];
    o->len = len;
    return 0;
}

/* ---------- 송신: Text Frame (len 무관) ---------- */
size_t ws_build_text_frame(const uint8_t *msg, size_t len, uint8_t *buf) {
    size_t pos = 0;

    // 1바이트: FIN=1, RSV1~3=0, opcode=0x1(Text)
    buf[pos++] = 0x80 | 0x1;

    // 2바이트 이상: payload length
    if (len < 126) {
        // 7비트로 바로 길이 표시
        buf[pos++] = (uint8_t)len;
    } else if (len <= 0xFFFF) {
        // 126 + 16비트 길이
        buf[pos++] = 126;
        buf[pos++] = (len >> 8) & 0xFF;
        buf[pos++] = len & 0xFF;
    } else {
        // 127 + 64비트 길이 (network byte order)
        buf[pos++] = 127;
        for (int i = 7; i >= 0; --i) {
            buf[pos++] = (len >> (8 * i)) & 0xFF;
        }
    }

    // payload 복사
    if (len > 0) {
        memcpy(buf + pos, msg, len);
        pos += len;
    }

    return pos;
}
