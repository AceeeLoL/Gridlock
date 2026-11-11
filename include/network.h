#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include <stddef.h>

bool send_all(int fd, const char *message);
int receive_line(int fd, char *buffer, size_t size);

#endif