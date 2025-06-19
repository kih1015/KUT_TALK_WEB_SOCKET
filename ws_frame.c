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

    o->fin    = hdr[0] & 0x80;
    o->opcode = hdr[0] & 0x0F;
    int mask  = hdr[1] & 0x80;
    uint64_t len = hdr[1] & 0x7F;

    if (len == 126) {
        uint16_t l16; readn(fd, &l16, 2);
        len = ntohs(l16);
    } else if (len == 127) {
        uint64_t l64; readn(fd, &l64, 8);
        len = be64toh(l64);
    }
    uint8_t mkey[4];
    if (mask) readn(fd, mkey, 4);

    o->payload = malloc(len);
    if (!o->payload) return -1;
    if (readn(fd, o->payload, len) != (ssize_t)len) return -1;

    if (mask)
        for (uint64_t i = 0; i < len; ++i)
            o->payload[i] ^= mkey[i & 3];
    o->len = len;
    return 0;
}

/* ---------- 송신 (len ≤ 125 기준) ---------- */
size_t ws_build_text_frame(const uint8_t *msg, size_t len, uint8_t *out) {
    out[0] = 0x81;          // FIN=1, opcode=0x1(Text)
    out[1] = len & 0x7F;    // MASK=0, len
    memcpy(out + 2, msg, len);
    return 2 + len;
}
