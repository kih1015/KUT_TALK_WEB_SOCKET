#pragma once
#include <stdint.h>
#include <unistd.h>
ssize_t readn(int fd, void *buf, size_t n);
ssize_t writen(int fd, const void *buf, size_t n);