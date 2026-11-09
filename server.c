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
#define CELL_MYSTERY '?'

/* Ability costs */
#define COST_MOVE 0
#define COST_SPRINT 2
#define COST_JUMP 3
#define COST_BOMB 5
#define MAX_POINTS 8

/* Mystery Blocks and Shrinking */
#define MYSTERY_INTERVAL 3
#define SHRINK_INTERVAL 10   /* Map shrinks every 10 cycles */

#define FREE_NONE 0
#define FREE_SPRINT 1
#define FREE_JUMP 2
#define FREE_BOMB 3
#define REWARD_NONE 0
#define REWARD_POINTS 1
#define REWARD_FREE 2
#define REWARD_LOST 3

typedef struct {
    int x, y;
} Position;

typedef struct {
    Position pos;
    int points;
    int id;
    int free_ability;
    int last_reward;
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

    game->p1 = (Player){ .pos = {7, 0}, .points = 0, .id = 1 };
    game->p2 = (Player){ .pos = {7, 14}, .points = 1, .id = 2 };

    game->grid[game->p1.pos.x][game->p1.pos.y] = CELL_P1;
    game->grid[game->p2.pos.x][game->p2.pos.y] = CELL_P2;
}

/* Randomly place a specific number of obstacles on empty cells. */
void generate_obstacles(GameState *game, int num_obstacles) {
    srand(time(NULL));
    int placed = 0;
    while (placed + 1 < num_obstacles) {
        int rx = rand() % BOARD_SIZE;
        int ry = rand() % (BOARD_SIZE / 2);
        int mx = rx;
        int my = BOARD_SIZE - 1 - ry;

        if (abs(rx - game->p1.pos.x) <= 1 && abs(ry - game->p1.pos.y) <= 1) continue;

        if (game->grid[rx][ry] == CELL_EMPTY && game->grid[mx][my] == CELL_EMPTY) {
            game->grid[rx][ry] = CELL_OBSTACLE;
            game->grid[mx][my] = CELL_OBSTACLE;
            placed += 2;
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
    return game->grid[p.x][p.y] == CELL_EMPTY || game->grid[p.x][p.y] == CELL_MYSTERY;
}

/* Spawn one Mystery Block */
void spawn_mystery_block(GameState *game) {
    for (int tries = 0; tries < 100; tries++) {
        int rx = rand() % BOARD_SIZE;
        int ry = rand() % BOARD_SIZE;
        if (game->grid[rx][ry] == CELL_EMPTY) {
            game->grid[rx][ry] = CELL_MYSTERY;
            return;
        }
    }
}

/* Give random reward for stepping on mystery block */
static void collect_mystery(GameState *game, Player *p, Position pos) {
    if (game->grid[pos.x][pos.y] != CELL_MYSTERY) return;

    int roll = rand() % 4;
    if (roll == 0) {
        p->points += 2;
        if (p->points > MAX_POINTS) p->points = MAX_POINTS;
        p->last_reward = REWARD_POINTS;
    } else if (p->free_ability != FREE_NONE) {
        p->last_reward = REWARD_LOST;
    } else {
        p->free_ability = roll;
        p->last_reward = REWARD_FREE;
    }
}

static const char *free_ability_name(int ability) {
    switch (ability) {
        case FREE_SPRINT: return "Sprint";
        case FREE_JUMP:   return "Jump";
        case FREE_BOMB:   return "Bomb";
        default:          return "none";
    }
}

/* Basic player movement: Returns 1 (Success), 0 (Invalid Try Again), or -1 (Fatal Crash) */
int move_player(GameState *game, Player *p, char dir) {
    int dx = 0, dy = 0;
    if (dir == 'W' || dir == 'w') dx = -1;
    else if (dir == 'S' || dir == 's') dx = 1;
    else if (dir == 'A' || dir == 'a') dy = -1;
    else if (dir == 'D' || dir == 'd') dy = 1;
    else return 0; // Not a direction, try again

    Position next = { p->pos.x + dx, p->pos.y + dy };
    
    // If you try to move into a wall or out of bounds, YOU CRASH AND DIE
    if (!is_valid_cell(game, next)) return -1; 

    collect_mystery(game, p, next);
    game->grid[p->pos.x][p->pos.y] = CELL_WALL;
    p->pos = next;
    game->grid[p->pos.x][p->pos.y] = (p->id == 1) ? CELL_P1 : CELL_P2;
    return 1;
}

/* Sprint 2 cells */
int ability_sprint(GameState *game, Player *p, char dir) {
    bool free_use = (p->free_ability == FREE_SPRINT);
    if (!free_use && p->points < COST_SPRINT) return 0; // Not enough points

    int dx = 0, dy = 0;
    if (dir == 'W' || dir == 'w') dx = -1;
    else if (dir == 'S' || dir == 's') dx = 1;
    else if (dir == 'A' || dir == 'a') dy = -1;
    else if (dir == 'D' || dir == 'd') dy = 1;
    else return 0;

    Position step1 = { p->pos.x + dx, p->pos.y + dy };
    Position step2 = { p->pos.x + 2*dx, p->pos.y + 2*dy };

    // Crashing into a wall mid-sprint or on landing is fatal
    if (!is_valid_cell(game, step1) || !is_valid_cell(game, step2)) return -1; 

    game->grid[p->pos.x][p->pos.y] = CELL_WALL;
    game->grid[step1.x][step1.y] = CELL_WALL;
    if (free_use) p->free_ability = FREE_NONE;
    else p->points -= COST_SPRINT;
    collect_mystery(game, p, step2);
    p->pos = step2;
    game->grid[p->pos.x][p->pos.y] = (p->id == 1) ? CELL_P1 : CELL_P2;
    return 1;
}

/* Jump over 1 wall */
int ability_jump(GameState *game, Player *p, char dir) {
    bool free_use = (p->free_ability == FREE_JUMP);
    if (!free_use && p->points < COST_JUMP) return 0;

    int dx = 0, dy = 0;
    if (dir == 'W' || dir == 'w') dx = -1;
    else if (dir == 'S' || dir == 's') dx = 1;
    else if (dir == 'A' || dir == 'a') dy = -1;
    else if (dir == 'D' || dir == 'd') dy = 1;
    else return 0;

    Position mid = { p->pos.x + dx, p->pos.y + dy };
    Position land = { p->pos.x + 2*dx, p->pos.y + 2*dy };

    if (mid.x < 0 || mid.x >= BOARD_SIZE || mid.y < 0 || mid.y >= BOARD_SIZE) return -1; // Crash out of bounds
    if (game->grid[mid.x][mid.y] != CELL_WALL) return 0; // You can only jump walls (invalid input)
    if (!is_valid_cell(game, land)) return -1; // Fatal crash on landing

    game->grid[p->pos.x][p->pos.y] = CELL_WALL;
    if (free_use) p->free_ability = FREE_NONE;
    else p->points -= COST_JUMP;
    collect_mystery(game, p, land);
    p->pos = land;
    game->grid[p->pos.x][p->pos.y] = (p->id == 1) ? CELL_P1 : CELL_P2;
    return 1;
}

/* Bomb 3x3 area */
int ability_bomb(GameState *game, Player *p) {
    bool free_use = (p->free_ability == FREE_BOMB);
    if (!free_use && p->points < COST_BOMB) return 0;
    if (game->current_cycle < 6) return 0;

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
    if (free_use) p->free_ability = FREE_NONE;
    else p->points -= COST_BOMB;
    return 1;
}

/* Shrink the board from the outside in */
void shrink_board(GameState *game, int client1, int client2) {
    int layer = (game->current_cycle / SHRINK_INTERVAL) - 1;
    
    if (layer < 0 || layer >= BOARD_SIZE / 2) return;

    for (int i = layer; i < BOARD_SIZE - layer; i++) {
        if (game->grid[layer][i] != CELL_P1 && game->grid[layer][i] != CELL_P2) 
            game->grid[layer][i] = CELL_OBSTACLE;
        if (game->grid[BOARD_SIZE - 1 - layer][i] != CELL_P1 && game->grid[BOARD_SIZE - 1 - layer][i] != CELL_P2) 
            game->grid[BOARD_SIZE - 1 - layer][i] = CELL_OBSTACLE;
            
        if (game->grid[i][layer] != CELL_P1 && game->grid[i][layer] != CELL_P2) 
            game->grid[i][layer] = CELL_OBSTACLE;
        if (game->grid[i][BOARD_SIZE - 1 - layer] != CELL_P1 && game->grid[i][BOARD_SIZE - 1 - layer] != CELL_P2) 
            game->grid[i][BOARD_SIZE - 1 - layer] = CELL_OBSTACLE;
    }

    send_all(client1, "\n>>> WARNING: THE MAP HAS SHRUNK! <<<\n");
    send_all(client2, "\n>>> WARNING: THE MAP HAS SHRUNK! <<<\n");
}

/* Build string for clients */
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

/* Check valid moves */
static bool has_valid_move(const GameState *game, const Player *p) {
    const char dirs[] = "WASD";

    for (int choice = 0; choice <= 2; choice++) {
        for (int d = 0; d < 4; d++) {
            GameState copy = *game;
            Player *cp = (p->id == 1) ? &copy.p1 : &copy.p2;
            int ok = 0;
            switch (choice) {
                case 0: ok = move_player(&copy, cp, dirs[d]); break;
                case 1: ok = ability_sprint(&copy, cp, dirs[d]); break;
                case 2: ok = ability_jump(&copy, cp, dirs[d]); break;
            }
            if (ok == 1) return true; // Only returns true if a move safely succeeds
        }
    }

    GameState copy = *game;
    Player *cp = (p->id == 1) ? &copy.p1 : &copy.p2;
    if (ability_bomb(&copy, cp) == 1) return true;

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

    printf("Gridlock server started on port %d.\n", port_no);

    /* --- INFINITE SERVER LOOP --- */
    while (1) {
        printf("\nWaiting for two players to connect...\n");
        
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
        generate_obstacles(&game, 12);
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

            bool current_ok = has_valid_move(&game, player);
            bool other_ok   = has_valid_move(&game, other);

            if (!current_ok && !other_ok) {
                send_all(clients[0], "OVER:Both players are trapped. It's a draw!\n");
                send_all(clients[1], "OVER:Both players are trapped. It's a draw!\n");
                break;
            }
            if (!current_ok) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "OVER:Player %d is trapped! Player %d wins!\n",
                         player->id, other->id);
                send_all(clients[0], msg);
                send_all(clients[1], msg);
                break;
            }

            char prompt[512];
            snprintf(prompt, sizeof(prompt),
                "Your turn (P%d | Points: %d | Free ability: %s | Cycle: %d)\n"
                "Actions: [0]Move [1]Sprint(2pt) [2]Jump(3pt) [3]Bomb(5pt,cycle6+)\n"
                "Format: <action> <dir:W/A/S/D> (Bomb needs no direction: just 3)\n"
                "INPUT\n",
                player->id, player->points, free_ability_name(player->free_ability),
                game.current_cycle);
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

            int valid = 0; // 0 = Error, 1 = Success, -1 = Fatal Crash
            if (fields >= 1) {
                switch (action) {
                    case 0: valid = move_player(&game, player, direction); break;
                    case 1: valid = ability_sprint(&game, player, direction); break;
                    case 2: valid = ability_jump(&game, player, direction); break;
                    case 3: valid = ability_bomb(&game, player); break;
                }
            }

            // Handle the result of the player's move
            if (valid == 0) {
                send_all(clients[active], "Invalid action. Check cost and format.\n");
                continue;
            } 
            else if (valid == -1) {
                // If they returned -1, they hit a wall. Game Over!
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "OVER:Player %d crashed into an obstacle! Player %d wins!\n",
                         player->id, other->id);
                send_all(clients[0], msg);
                send_all(clients[1], msg);
                break;
            }

            if (player->last_reward != REWARD_NONE) {
                char reward_msg[160];
                if (player->last_reward == REWARD_POINTS)
                    snprintf(reward_msg, sizeof(reward_msg), "Mystery Block: +2 points!\n");
                else if (player->last_reward == REWARD_FREE)
                    snprintf(reward_msg, sizeof(reward_msg),
                             "Mystery Block: a free %s! It is used automatically with that action.\n",
                             free_ability_name(player->free_ability));
                else
                    snprintf(reward_msg, sizeof(reward_msg),
                             "Mystery Block: you already hold a free ability, so this reward was lost.\n");
                send_all(clients[active], reward_msg);
                player->last_reward = REWARD_NONE;
            }

            active = 1 - active;

            if (active == 0) {
                game.current_cycle++;
                if (game.p1.points < MAX_POINTS) game.p1.points++;
                if (game.p2.points < MAX_POINTS) game.p2.points++;
                if (game.current_cycle % MYSTERY_INTERVAL == 0) spawn_mystery_block(&game);
                
                if (game.current_cycle % SHRINK_INTERVAL == 0) {
                    shrink_board(&game, clients[0], clients[1]);
                }
            }

            print_board(&game);
        }

        /* End of game cleanup */
        for (int i = 0; i < 2; i++) close(clients[i]);
        printf("Match finished. Resetting server for the next game...\n");
    }

    close(server_fd);
    return 0;
}
