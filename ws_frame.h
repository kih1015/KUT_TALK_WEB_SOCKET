#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t fin;
    uint8_t opcode;
    uint64_t len;
    uint8_t *payload;
} ws_frame_t;

int ws_recv(int fd, ws_frame_t *out);

size_t ws_build_text_frame(const uint8_t *msg, size_t len, uint8_t *out);

size_t ws_build_control_frame(uint8_t opcode, const uint8_t *payload, size_t len, uint8_t *buf);

size_t ws_build_frame(uint8_t opcode,
                      const uint8_t *payload,
                      size_t len,
                      uint8_t *buf);
