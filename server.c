#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

#define BOARD_SIZE 15
#define CELL_EMPTY '.'
#define CELL_WALL '#'
#define CELL_OBSTACLE 'X'
#define CELL_P1 '1'
#define CELL_P2 '2'

typedef struct {
    int x, y;
} Position;

typedef struct {
    Position pos;
    int points;
    int id;
} Player;

typedef struct {
    char grid[BOARD_SIZE][BOARD_SIZE];
    Player p1;
    Player p2;
    int current_cycle;
} GameState;

/* Send all bytes, even if send() writes only part of the message. */
static bool send_all(int fd, const char *message) {
    size_t total = strlen(message), sent = 0;
    while (sent < total) {
        ssize_t n = send(fd, message + sent, total - sent, 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

/* Receive one newline-terminated command from a client. */
static int receive_line(int fd, char *buffer, size_t size) {
    size_t used = 0;
    while (used + 1 < size) {
        char ch;
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n == 0) return 0;          /* client disconnected */
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

/* Initialize the board grid, player start positions, and empty state */
void init_game(GameState *game) {
    memset(game->grid, CELL_EMPTY, sizeof(game->grid));
    game->current_cycle = 1;

    /* Initial player positions */
    game->p1 = (Player){ .pos = {7, 0}, .points = 0, .id = 1 };
    game->p2 = (Player){ .pos = {7, 14}, .points = 1, .id = 2 };

    game->grid[game->p1.pos.x][game->p1.pos.y] = CELL_P1;
    game->grid[game->p2.pos.x][game->p2.pos.y] = CELL_P2;
}

/* Randomly place a specific number of obstacles on empty cells */
void generate_obstacles(GameState *game, int num_obstacles) {
    srand(time(NULL)); 
    
    int placed = 0;
    while (placed < num_obstacles) {
        int rx = rand() % BOARD_SIZE;
        int ry = rand() % BOARD_SIZE;
        
        if (game->grid[rx][ry] == CELL_EMPTY) {
            game->grid[rx][ry] = CELL_OBSTACLE;
            placed++;
        }
    }
}

/* Print the current board state to stdout for debugging */
void print_board(const GameState *game) {
    printf("\n--- GRIDLOCK (Cycle %d) ---\n  ", game->current_cycle);
    for (int j = 0; j < BOARD_SIZE; j++) {
        printf("%d ", j % 10);
    }
    printf("\n");

    for (int i = 0; i < BOARD_SIZE; i++) {
        printf("%d ", i % 10);
        for (int j = 0; j < BOARD_SIZE; j++) {
            printf("%c ", game->grid[i][j]);
        }
        printf("\n");
    }
}

/* Check if a cell is within bounds and unoccupied */
bool is_valid_cell(const GameState *game, Position p) {
    if (p.x < 0 || p.x >= BOARD_SIZE || p.y < 0 || p.y >= BOARD_SIZE) return false;
    return game->grid[p.x][p.y] == CELL_EMPTY;
}

/* Basic player movement helper (standard move only) */
bool move_player(GameState *game, Player *p, char dir) {
    int dx = 0, dy = 0;
    if (dir == 'W' || dir == 'w') dx = -1;
    else if (dir == 'S' || dir == 's') dx = 1;
    else if (dir == 'A' || dir == 'a') dy = -1;
    else if (dir == 'D' || dir == 'd') dy = 1;
    else return false;

    Position next = { p->pos.x + dx, p->pos.y + dy };
    if (!is_valid_cell(game, next)) return false;

    /* Leave a trail behind */
    game->grid[p->pos.x][p->pos.y] = CELL_WALL;
    p->pos = next;
    game->grid[p->pos.x][p->pos.y] = (p->id == 1) ? CELL_P1 : CELL_P2;
    return true;
}
    
static void make_board_message(const GameState *game, char *out, size_t size) {
    size_t used = 0;
    int n = snprintf(out, size,
        "\n--- GRIDLOCK (Cycle %d) ---\nP1 Points: %d | P2 Points: %d\n\n  ",
        game->current_cycle, game->p1.points, game->p2.points);
    if (n < 0 || (size_t)n >= size) { out[0] = '\0'; return; }
    used = (size_t)n;

    for (int j = 0; j < BOARD_SIZE && used < size; j++) {
        n = snprintf(out + used, size - used, "%d ", j % 10);
        if (n < 0 || (size_t)n >= size - used) return;
        used += (size_t)n;
    }
    n = snprintf(out + used, size - used, "\n");
    if (n < 0 || (size_t)n >= size - used) return;
    used += (size_t)n;

    for (int i = 0; i < BOARD_SIZE && used < size; i++) {
        n = snprintf(out + used, size - used, "%d ", i % 10);
        if (n < 0 || (size_t)n >= size - used) return;
        used += (size_t)n;
        for (int j = 0; j < BOARD_SIZE; j++) {
            n = snprintf(out + used, size - used, "%c ", game->grid[i][j]);
            if (n < 0 || (size_t)n >= size - used) return;
            used += (size_t)n;
        }
        n = snprintf(out + used, size - used, "\n");
        if (n < 0 || (size_t)n >= size - used) return;
        used += (size_t)n;
    }
}


int main(int argc, char *argv[]) {
    
    // Check if port number was provided
    if (argc < 2) {
        printf("Usage: %s port_no\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Get port number from command-line argument
    int port_no = atoi(argv[1]);

    if (port_no < 1 || port_no > 65535) {
        printf("Invalid port number.\n");
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);

    /* create, bind, and listen on the TCP server socket. */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return EXIT_FAILURE; }

    int option = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_no);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind"); close(server_fd); return EXIT_FAILURE;
    }
    if (listen(server_fd, 2) < 0) {
        perror("listen"); close(server_fd); return EXIT_FAILURE;
    }

    printf("Gridlock server listening on port %d. Waiting for two players...\n", port_no);
    int clients[2];
    for (int i = 0; i < 2; i++) {
        clients[i] = accept(server_fd, NULL, NULL);
        if (clients[i] < 0) {
            perror("accept");
            for (int j = 0; j < i; j++) close(clients[j]);
            close(server_fd); return EXIT_FAILURE;
        }
        printf("Player %d connected.\n", i + 1);
    }

    GameState game;
    init_game(&game); /* Server owns the game state. */
    generate_obstacles(&game, 20);
    send_all(clients[0], "START:1\nGame started. You are Player 1.\n");
    send_all(clients[1], "START:2\nGame started. You are Player 2.\n");

    int active = 0;
    char board_message[2048];
    char command[128];
    bool running = true;

    /* send board + INPUT prompt, receive command, and update turns. */
    while (running) {
        Player *player = (active == 0) ? &game.p1 : &game.p2;
        make_board_message(&game, board_message, sizeof(board_message));
        send_all(clients[0], board_message);
        send_all(clients[1], board_message);

        send_all(clients[active], "Your turn. Enter: 0 W/A/S/D\nINPUT\n");
        send_all(clients[1 - active], "WAIT:Opponent's turn...\n");

        int received = receive_line(clients[active], command, sizeof(command));
        if (received <= 0) {
            printf("Player %d disconnected.\n", player->id);
            send_all(clients[1 - active], "OVER:Opponent disconnected.\n");
            break;
        }

        int action = -1;
        char direction = '\0';
        if (sscanf(command, "%d %c", &action, &direction) != 2 || action != 0 ||
            !move_player(&game, player, direction)) {
            send_all(clients[active], "Invalid move. Use action 0 and W/A/S/D.\n");
            continue; /* same player retries */
        }

        active = 1 - active;
        game.current_cycle++;
        print_board(&game);
    }

    for (int i = 0; i < 2; i++) close(clients[i]);
    close(server_fd);
    return 0;
}
