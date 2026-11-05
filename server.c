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

/* Ability costs */
#define COST_MOVE 0
#define COST_SPRINT 2
#define COST_JUMP 3
#define COST_BOMB 5
#define MAX_POINTS 8

typedef struct {
    int x, y;
} Position;

typedef struct {
    Position pos;
    int points;
    int id;
    bool derezzed; /* Track if the player crashed */
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

/* Initialize the board grid, player start positions, and empty state */
void init_game(GameState *game) {
    memset(game->grid, CELL_EMPTY, sizeof(game->grid));
    game->current_cycle = 1;

    game->p1 = (Player){ .pos = {7, 0}, .points = 0, .id = 1, .derezzed = false };
    game->p2 = (Player){ .pos = {7, 14}, .points = 1, .id = 2, .derezzed = false };

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
    for (int j = 0; j < BOARD_SIZE; j++) printf("%d ", j % 10);
    printf("\n");
    for (int i = 0; i < BOARD_SIZE; i++) {
        printf("%d ", i % 10);
        for (int j = 0; j < BOARD_SIZE; j++) printf("%c ", game->grid[i][j]);
        printf("\n");
    }
}

/* Check if a cell is within bounds and unoccupied */
bool is_valid_cell(const GameState *game, Position p) {
    if (p.x < 0 || p.x >= BOARD_SIZE || p.y < 0 || p.y >= BOARD_SIZE) return false;
    return game->grid[p.x][p.y] == CELL_EMPTY;
}

/* Standard movement: Fatal crash if hitting boundary, wall, obstacle, or player */
bool move_player(GameState *game, Player *p, Player *opponent, char dir) {
    int dx = 0, dy = 0;
    if (dir == 'W' || dir == 'w') dx = -1;
    else if (dir == 'S' || dir == 's') dx = 1;
    else if (dir == 'A' || dir == 'a') dy = -1;
    else if (dir == 'D' || dir == 'd') dy = 1;
    else return false;

    Position next = { p->pos.x + dx, p->pos.y + dy };

    /* Fatal Collision Check */
    if (!is_valid_cell(game, next)) {
        p->derezzed = true;
        if (next.x == opponent->pos.x && next.y == opponent->pos.y) {
            opponent->derezzed = true;
        }
        return true;
    }

    game->grid[p->pos.x][p->pos.y] = CELL_WALL;
    p->pos = next;
    game->grid[p->pos.x][p->pos.y] = (p->id == 1) ? CELL_P1 : CELL_P2;
    return true;
}

/* Sprint: Move 2 cells. Crashes if either step hits a collision */
bool ability_sprint(GameState *game, Player *p, Player *opponent, char dir) {
    if (p->points < COST_SPRINT) return false;

    int dx = 0, dy = 0;
    if (dir == 'W' || dir == 'w') dx = -1;
    else if (dir == 'S' || dir == 's') dx = 1;
    else if (dir == 'A' || dir == 'a') dy = -1;
    else if (dir == 'D' || dir == 'd') dy = 1;
    else return false;

    Position step1 = { p->pos.x + dx, p->pos.y + dy };
    Position step2 = { p->pos.x + 2*dx, p->pos.y + 2*dy };

    /* Step 1 collision */
    if (!is_valid_cell(game, step1)) {
        p->derezzed = true;
        if (step1.x == opponent->pos.x && step1.y == opponent->pos.y) opponent->derezzed = true;
        p->points -= COST_SPRINT;
        return true;
    }

    /* Step 2 collision */
    if (!is_valid_cell(game, step2)) {
        game->grid[p->pos.x][p->pos.y] = CELL_WALL;
        p->derezzed = true;
        if (step2.x == opponent->pos.x && step2.y == opponent->pos.y) opponent->derezzed = true;
        p->points -= COST_SPRINT;
        return true;
    }

    game->grid[p->pos.x][p->pos.y] = CELL_WALL;
    game->grid[step1.x][step1.y] = CELL_WALL;
    p->pos = step2;
    game->grid[p->pos.x][p->pos.y] = (p->id == 1) ? CELL_P1 : CELL_P2;
    p->points -= COST_SPRINT;
    return true;
}

/* Jump: Hop over 1 wall and land on the cell beyond. Crashes if landing cell is invalid */
bool ability_jump(GameState *game, Player *p, Player *opponent, char dir) {
    if (p->points < COST_JUMP) return false;

    int dx = 0, dy = 0;
    if (dir == 'W' || dir == 'w') dx = -1;
    else if (dir == 'S' || dir == 's') dx = 1;
    else if (dir == 'A' || dir == 'a') dy = -1;
    else if (dir == 'D' || dir == 'd') dy = 1;
    else return false;

    Position mid = { p->pos.x + dx, p->pos.y + dy };
    Position land = { p->pos.x + 2*dx, p->pos.y + 2*dy };

    if (mid.x < 0 || mid.x >= BOARD_SIZE || mid.y < 0 || mid.y >= BOARD_SIZE || game->grid[mid.x][mid.y] != CELL_WALL) {
        return false;
    }

    if (!is_valid_cell(game, land)) {
        p->derezzed = true;
        if (land.x == opponent->pos.x && land.y == opponent->pos.y) opponent->derezzed = true;
        p->points -= COST_JUMP;
        return true;
    }

    game->grid[p->pos.x][p->pos.y] = CELL_WALL;
    p->pos = land;
    game->grid[p->pos.x][p->pos.y] = (p->id == 1) ? CELL_P1 : CELL_P2;
    p->points -= COST_JUMP;
    return true;
}

/* Bomb: Destroys all trail walls in 3x3 area around player */
bool ability_bomb(GameState *game, Player *p) {
    if (p->points < COST_BOMB) return false;
    if (game->current_cycle < 6) return false;

    for (int r = p->pos.x - 1; r <= p->pos.x + 1; r++) {
        for (int c = p->pos.y - 1; c <= p->pos.y + 1; c++) {
            if (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE) {
                if (r == p->pos.x && c == p->pos.y) continue;
                if (game->grid[r][c] == CELL_WALL) {
                    game->grid[r][c] = CELL_EMPTY;
                }
            }
        }
    }
    p->points -= COST_BOMB;
    return true;
}

/* Build network-friendly board string for clients */
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

/* Check if player has any safe non-crashing move */
static bool has_safe_move(const GameState *game, const Player *p) {
    const char dirs[] = "WASD";

    for (int choice = 0; choice <= 2; choice++) {
        for (int d = 0; d < 4; d++) {
            GameState copy = *game;
            Player *cp = (p->id == 1) ? &copy.p1 : &copy.p2;
            Player *co = (p->id == 1) ? &copy.p2 : &copy.p1;
            bool ok = false;
            switch (choice) {
                case 0: ok = move_player(&copy, cp, co, dirs[d]); break;
                case 1: ok = ability_sprint(&copy, cp, co, dirs[d]); break;
                case 2: ok = ability_jump(&copy, cp, co, dirs[d]); break;
            }
            if (ok && !cp->derezzed) return true;
        }
    }

    GameState copy = *game;
    Player *cp = (p->id == 1) ? &copy.p1 : &copy.p2;
    if (ability_bomb(&copy, cp)) return true;

    return false;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s port_no\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port_no = atoi(argv[1]);
    if (port_no < 1 || port_no > 65535) {
        printf("Invalid port number.\n");
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);

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
    init_game(&game);
    generate_obstacles(&game, 20);
    send_all(clients[0], "START:1\nGame started. You are Player 1.\n");
    send_all(clients[1], "START:2\nGame started. You are Player 2.\n");

    int active = 0;
    char board_message[2048];
    char command[128];
    bool running = true;

    while (running) {
        Player *player = (active == 0) ? &game.p1 : &game.p2;
        Player *other  = (active == 0) ? &game.p2 : &game.p1;

        make_board_message(&game, board_message, sizeof(board_message));
        send_all(clients[0], board_message);
        send_all(clients[1], board_message);

        if (!has_safe_move(&game, player)) {
            send_all(clients[active], "WARNING: You are completely trapped! Any move will derez you.\n");
        }

        char prompt[512];
        snprintf(prompt, sizeof(prompt),
            "Your turn (P%d | Points: %d | Cycle: %d)\n"
            "Actions: [0]Move [1]Sprint(2pt) [2]Jump(3pt) [3]Bomb(5pt,cycle6+)\n"
            "Format: <action> <dir:W/A/S/D> (Bomb needs no direction: just 3)\n"
            "INPUT\n",
            player->id, player->points, game.current_cycle);
        send_all(clients[active], prompt);
        send_all(clients[1 - active], "WAIT:Opponent's turn...\n");

        int received = receive_line(clients[active], command, sizeof(command));
        if (received <= 0) {
            printf("Player %d disconnected.\n", player->id);
            send_all(clients[1 - active], "OVER:Opponent disconnected.\n");
            break;
        }

        int action = -1;
        char direction = '\0';
        int fields = sscanf(command, "%d %c", &action, &direction);

        bool valid = false;
        if (fields >= 1) {
            switch (action) {
                case 0: /* Move */
                    valid = move_player(&game, player, other, direction);
                    break;
                case 1: /* Sprint */
                    valid = ability_sprint(&game, player, other, direction);
                    break;
                case 2: /* Jump */
                    valid = ability_jump(&game, player, other, direction);
                    break;
                case 3: /* Bomb */
                    valid = ability_bomb(&game, player);
                    break;
            }
        }

        if (!valid) {
            send_all(clients[active], "Invalid action or format. Check point cost.\n");
            continue;
        }

        /* Win/Draw/Derez state evaluation */
        if (player->derezzed && other->derezzed) {
            send_all(clients[0], "OVER:Head-on collision! Both players derezzed. It's a draw!\n");
            send_all(clients[1], "OVER:Head-on collision! Both players derezzed. It's a draw!\n");
            break;
        }
        if (player->derezzed) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "OVER:Player %d crashed! Player %d wins!\n",
                     player->id, other->id);
            send_all(clients[0], msg);
            send_all(clients[1], msg);
            break;
        }

        active = 1 - active;

        if (active == 0) {
            game.current_cycle++;
            if (game.p1.points < MAX_POINTS) game.p1.points++;
            if (game.p2.points < MAX_POINTS) game.p2.points++;
        }

        print_board(&game);
    }

    for (int i = 0; i < 2; i++) close(clients[i]);
    close(server_fd);
    return 0;
}
