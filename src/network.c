#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include "network.h"

bool send_all(int fd, const char *message) {
    size_t total = strlen(message), sent = 0;
    while (sent < total) {
        ssize_t n = send(fd, message + sent, total - sent, 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

int receive_line(int fd, char *buffer, size_t size) {
    size_t used = 0;
    while (used + 1 < size) {
        char ch;
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n == 0) return 0;
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (ch == '\n') break;
        if (ch != '\r') buffer[used++] = ch;
    }
    buffer[used] = '\0';
    return 1;
}