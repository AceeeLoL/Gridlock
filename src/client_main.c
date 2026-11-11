#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include "network.h"

#define BUFFER_SIZE 4096

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s hostname port_no\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *server_ip = argv[1];
    int port_no = atoi(argv[2]);
    struct hostent *server = gethostbyname(server_ip);
    if (server == NULL) { printf("Error: No such host.\n"); return EXIT_FAILURE; }

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) { perror("socket"); return 1; }

    struct sockaddr_in server_address = { .sin_family = AF_INET, .sin_port = htons(port_no) };
    memcpy(&server_address.sin_addr.s_addr, server->h_addr, server->h_length);

    printf("Connecting to Gridlock server...\n");
    if (connect(client_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Connection failed");
        close(client_socket);
        return 1;
    }
    printf("Connected to Gridlock server!\n");

    char buffer[BUFFER_SIZE];
    while (1) {
        if (receive_line(client_socket, buffer, BUFFER_SIZE) <= 0) {
            printf("Server disconnected.\n");
            break;
        }
        printf("%s\n", buffer);
        if (strncmp(buffer, "START:", 6) == 0) {
            printf("Legend: 1 = Player 1, 2 = Player 2, # = trail wall, X = obstacle, ? = mystery block, . = empty\n");
        }
        if (strncmp(buffer, "OVER:", 5) == 0) break;
        if (strcmp(buffer, "INPUT") == 0) {
            char input[100];
            printf("Enter action (0 W = move, 1 D = sprint, 2 A = jump, 3 = bomb): ");
            fflush(stdout);
            if (fgets(input, sizeof(input), stdin) == NULL) break;
            send(client_socket, input, strlen(input), 0);
        }
    }
    close(client_socket);
    return 0;
}
