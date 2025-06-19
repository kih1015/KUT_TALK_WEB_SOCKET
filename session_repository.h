#pragma once
#include <stdint.h>
#include <time.h>

int session_repository_find_id(const char *sid,
                               uint32_t *out_user_id,
                               time_t *out_exp);
