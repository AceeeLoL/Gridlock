
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <termios.h>  // NEW: Provides tcflush() and TCIFLUSH for clearing terminal input.
#include "network.h"

#define BUFFER_SIZE 4096

/* ANSI terminal styles: no external color library is required. */
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define BLUE    "\033[34m"
#define YELLOW  "\033[33m"
#define RED     "\033[31m"
#define PURPLE  "\033[35m"
#define GRAY    "\033[90m"
#define CYAN    "\033[36m"
#define MAGENTA "\033[35m"
#define GREEN   "\033[32m"
#define ORANGE  "\033[38;5;208m"
#define WHITE   "\033[37m"


static void discard_pending_terminal_input(void) {
    if (isatty(STDIN_FILENO)) {
        if (tcflush(STDIN_FILENO, TCIFLUSH) == -1 && errno != ENOTTY) {
            perror("tcflush");
        }
    }
}


static void print_board_line(const char *line) {
    if (strstr(line, "--- GRIDLOCK") != NULL) {
        printf("\n%s%s%s\n", BOLD, line, RESET);
        return;
    }

    /* Board rows begin with a row number followed by a space. */
    if (line[0] >= '0' && line[0] <= '9' && line[1] == ' ') {
        printf("%s%c%s ", WHITE, line[0], RESET);

        for (size_t i = 2; line[i] != '\0'; ++i) {
            const char *color = RESET;

            switch (line[i]) {
                case '1': color = BLUE; break;
                case '2': color = YELLOW; break;
                case 'X': color = RED; break;
                case '?': color = PURPLE; break;
                case 'a': color = BLUE; break;
                case 'b': color = YELLOW; break;
                case '#': color = GRAY; break;
                default: break;
            }

            if (line[i] == ' ')
                putchar(' ');
            else if (line[i] == 'a' || line[i] == 'b')
                printf("%s#%s", color, RESET);
            else
                printf("%s%c%s", color, line[i], RESET);
        }

        putchar('\n');
        return;
    }

    /* Column headings and other board coordinates. */
    if (line[0] == ' ' || line[0] == '\0') {
        printf("%s%s%s\n", WHITE, line, RESET);
        return;
    }

    printf("%s\n", line);
}


static void print_server_message(const char *message) {
    if (strncmp(message, "START:", 6) == 0) {
        printf("%s%s%s\n", CYAN, message + 6, RESET);

    } else if (strncmp(message, "WAIT:", 5) == 0) {
        printf("%s%s%s\n", CYAN, message + 5, RESET);

    } else if (strncmp(message, "INVALID:", 8) == 0) {
        printf("%s%s%s\n", RED, message + 8, RESET);

    } else if (strncmp(message, "CAUTION:", 8) == 0) {
        printf("%s%s%s\n", ORANGE, message, RESET);

    } else if (strncmp(message, "ROUND_OVER:", 11) == 0) {
        const char *body = message + 11;
        const char *round_label = strstr(body, "Round ");
        const char *winner_label = strstr(body, "Player ");

        if (round_label == body) {
            const char *colon = strchr(round_label, ':');

            if (colon != NULL) {
                printf("%s%s%.*s%s%s%s\n",
                       MAGENTA, BOLD,
                       (int)(colon - round_label + 1),
                       round_label, RESET, MAGENTA, colon + 1);
            } else {
                printf("%s%s%s%s\n", MAGENTA, BOLD, body, RESET);
            }
        } else {
            printf("%s%s%s%s\n", MAGENTA, BOLD, body, RESET);
        }

        if (winner_label != NULL && strstr(winner_label, "wins") != NULL) {
            /* Highlight the winner separately. */
            const char *end = strchr(winner_label, '.');

            if (end == NULL)
                end = winner_label + strlen(winner_label);

            printf("%s%s%.*s%s\n",
                   GREEN, BOLD,
                   (int)(end - winner_label),
                   winner_label, RESET);
        }

    } else if (strncmp(message, "MATCH_RESULT:", 13) == 0) {
        printf("%s%s%s%s\n", GREEN, BOLD, message + 13, RESET);

    } else if (strncmp(message, "CONNECTED:", 10) == 0) {
        printf("%s%s%s\n", MAGENTA, message + 10, RESET);

    } else if (strncmp(message, "STATE:", 6) == 0) {
        printf("%s%s%s\n", YELLOW, message + 6, RESET);

    } else if (strncmp(message, "Mystery Block:", 14) == 0) {
        printf("%s%s%s\n", PURPLE, message, RESET);

    } else if (strncmp(message, "OVER:", 5) == 0) {
        printf("%s%s%s\n", MAGENTA, message + 5, RESET);

    } else {
        printf("%s\n", message);
    }
}


int main(int argc, char *argv[]) {

    if (argc < 3) {
        printf("Usage: %s hostname port_no\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *server_ip = argv[1];
    int port_no = atoi(argv[2]);

    struct hostent *server = gethostbyname(server_ip);

    if (server == NULL) {
        printf("Error: No such host.\n");
        return EXIT_FAILURE;
    }

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (client_socket < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_address = {
        .sin_family = AF_INET,
        .sin_port = htons(port_no)
    };

    memcpy(&server_address.sin_addr.s_addr,
           server->h_addr, server->h_length);

    printf("Connecting to Gridlock server...\n");

    if (connect(client_socket,
                (struct sockaddr *)&server_address,
                sizeof(server_address)) < 0) {

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

        if (strncmp(buffer, "RESTART_PROMPT:", 15) == 0) {

            char input[100];

            // NEW: Clear any old keyboard input before accepting restart input.
            discard_pending_terminal_input();

            printf("%s%s%s\n", CYAN, buffer + 15, RESET);
            fflush(stdout);

            if (fgets(input, sizeof(input), stdin) == NULL)
                break;

            send(client_socket, input, strlen(input), 0);
            continue;
        }

        if (strncmp(buffer, "--- GRIDLOCK", 12) == 0 ||
            (buffer[0] >= '0' && buffer[0] <= '9' && buffer[1] == ' ') ||
            buffer[0] == ' ' || buffer[0] == '\0') {

            print_board_line(buffer);

        } else {
            print_server_message(buffer);
        }

        if (strncmp(buffer, "START:", 6) == 0) {
            printf("%sLegend: 1 = Player 1, 2 = Player 2, "
                   "# = trail wall, X = obstacle, "
                   "? = mystery block, . = empty%s\n",
                   CYAN, RESET);
        }

        if (strncmp(buffer, "OVER:", 5) == 0)
            break;

        if (strcmp(buffer, "INPUT") == 0) {

            char input[100];


            discard_pending_terminal_input();

            printf("Enter action "
                   "(0 W = move, 1 D = sprint, "
                   "2 A = jump, 3 = bomb): ");

            fflush(stdout);

            if (fgets(input, sizeof(input), stdin) == NULL)
                break;

            send(client_socket, input, strlen(input), 0);
        }
    }

    close(client_socket);

    return 0;
}
