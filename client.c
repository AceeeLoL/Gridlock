#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>

#define BUFFER_SIZE 4096


int read_line(int socket_fd, char *buffer, int size) {
    int i = 0;
    char ch;

    while (i < size - 1) {
        int n = recv(socket_fd, &ch, 1, 0);

        if (n == 0) {
            return 0; // Server disconnected
        }

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1; // Receiving error
        }

        if (ch == '\n') {
            break;
        }

        if (ch != '\r') {
            buffer[i++] = ch;
        }
    }

    buffer[i] = '\0';
    return 1; // Successfully received a line
}

int main(int argc, char *argv[]) {

    // Check if hostname and port were provided
    if (argc < 3) {
        printf("Usage: %s hostname port_no\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Get hostname and port from command-line arguments
    char *server_ip = argv[1];
    int port_no = atoi(argv[2]);

    if (port_no < 1 || port_no > 65535) {
        printf("Invalid port number.\n");
        return EXIT_FAILURE;
    }

    // Resolve server hostname/IP
    struct hostent *server = gethostbyname(server_ip);

    if (server == NULL) {
        printf("Error: No such host.\n");
        return EXIT_FAILURE;
    }

    int client_socket;
    struct sockaddr_in server_address;
    char buffer[BUFFER_SIZE];

    // Create TCP socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (client_socket < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // Configure server address
    memset(&server_address, 0, sizeof(server_address));

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port_no);

    memcpy(&server_address.sin_addr.s_addr,
       server->h_addr,
       server->h_length);

    // Connect to server
    printf("Connecting to Gridlock server...\n");

    if (connect(client_socket,
                (struct sockaddr *)&server_address,
                sizeof(server_address)) < 0) {
        perror("Connection failed");
        close(client_socket);
        return 1;
    }

    printf("Connected to Gridlock server!\n");

    // Receive messages from server
    while (1) {
        int bytes_received = read_line(
            client_socket,
            buffer,
            BUFFER_SIZE
        );

        if (bytes_received <= 0) {
            printf("Server disconnected.\n");
            break;
        }

        // Display server message
        printf("%s\n", buffer);

        // Show the board legend once, when the game starts
        if (strncmp(buffer, "START:", 6) == 0) {
            printf("Legend: 1 = Player 1, 2 = Player 2, # = trail wall, "
                   "X = obstacle, . = empty\n");
        }

        // Stop if game is over
        if (strncmp(buffer, "OVER:", 5) == 0) {
            break;
        }

        // Wait for player input
        if (strcmp(buffer, "INPUT") == 0) {
            char input[100];

            printf("Enter action (e.g. 0 W = move, 1 D = sprint, 2 A = jump, 3 = bomb): ");
            fflush(stdout);

            if (fgets(input, sizeof(input), stdin) == NULL) {
                break;
            }

            // Send input to server
            send(client_socket, input, strlen(input), 0);
        }
    }

    close(client_socket);

    printf("Client disconnected.\n");

    return 0;
}
