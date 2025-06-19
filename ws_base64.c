#include "ws_base64.h"

#include <stdint.h>

static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz0123456789+/";

void base64_encode(const unsigned char *in, int len, char *out) {
    int i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = i < len ? in[i++] : 0;
        uint32_t octet_b = i < len ? in[i++] : 0;
        uint32_t octet_c = i < len ? in[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        out[j++] = tbl[(triple >> 18) & 0x3F];
        out[j++] = tbl[(triple >> 12) & 0x3F];
        out[j++] = (i - 2) > len ? '=' : tbl[(triple >> 6) & 0x3F];
        out[j++] = (i - 1) > len ? '=' : tbl[triple & 0x3F];
    }
    out[j] = '\0';
}
