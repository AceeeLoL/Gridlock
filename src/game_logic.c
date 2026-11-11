#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "game_logic.h"
#include "network.h"

void init_game(GameState *game) {
    memset(game->grid, CELL_EMPTY, sizeof(game->grid));
    game->current_cycle = 1;
    game->p1 = (Player){ .pos = {7, 0}, .points = 0, .id = 1, .free_ability = FREE_NONE, .last_reward = REWARD_NONE };
    game->p2 = (Player){ .pos = {7, 14}, .points = 1, .id = 2, .free_ability = FREE_NONE, .last_reward = REWARD_NONE };
    game->grid[game->p1.pos.x][game->p1.pos.y] = CELL_P1;
    game->grid[game->p2.pos.x][game->p2.pos.y] = CELL_P2;
}

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

bool is_valid_cell(const GameState *game, Position p) {
    if (p.x < 0 || p.x >= BOARD_SIZE || p.y < 0 || p.y >= BOARD_SIZE) return false;
    return game->grid[p.x][p.y] == CELL_EMPTY || game->grid[p.x][p.y] == CELL_MYSTERY;
}

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

const char *free_ability_name(int ability) {
    switch (ability) {
        case FREE_SPRINT: return "Sprint";
        case FREE_JUMP:   return "Jump";
        case FREE_BOMB:   return "Bomb";
        default:          return "none";
    }
}

int move_player(GameState *game, Player *p, char dir) {
    int dx = 0, dy = 0;
    if (dir == 'W' || dir == 'w') dx = -1;
    else if (dir == 'S' || dir == 's') dx = 1;
    else if (dir == 'A' || dir == 'a') dy = -1;
    else if (dir == 'D' || dir == 'd') dy = 1;
    else return 0;
    Position next = { p->pos.x + dx, p->pos.y + dy };
    if (!is_valid_cell(game, next)) return -1; 
    collect_mystery(game, p, next);
    game->grid[p->pos.x][p->pos.y] = CELL_WALL;
    p->pos = next;
    game->grid[p->pos.x][p->pos.y] = (p->id == 1) ? CELL_P1 : CELL_P2;
    return 1;
}

int ability_sprint(GameState *game, Player *p, char dir) {
    bool free_use = (p->free_ability == FREE_SPRINT);
    if (!free_use && p->points < COST_SPRINT) return 0;
    int dx = 0, dy = 0;
    if (dir == 'W' || dir == 'w') dx = -1;
    else if (dir == 'S' || dir == 's') dx = 1;
    else if (dir == 'A' || dir == 'a') dy = -1;
    else if (dir == 'D' || dir == 'd') dy = 1;
    else return 0;
    Position step1 = { p->pos.x + dx, p->pos.y + dy };
    Position step2 = { p->pos.x + 2*dx, p->pos.y + 2*dy };
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
    if (land.x < 0 || land.x >= BOARD_SIZE || land.y < 0 || land.y >= BOARD_SIZE) return -1;
    if (game->grid[land.x][land.y] != CELL_EMPTY && game->grid[land.x][land.y] != CELL_MYSTERY) return -1;
    game->grid[p->pos.x][p->pos.y] = CELL_WALL;
    if (free_use) p->free_ability = FREE_NONE;
    else p->points -= COST_JUMP;
    collect_mystery(game, p, land);
    p->pos = land;
    game->grid[p->pos.x][p->pos.y] = (p->id == 1) ? CELL_P1 : CELL_P2;
    return 1;
}

int ability_bomb(GameState *game, Player *p) {
    bool free_use = (p->free_ability == FREE_BOMB);
    if (!free_use && p->points < COST_BOMB) return 0;
    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            int nx = p->pos.x + i;
            int ny = p->pos.y + j;
            if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
                if (game->grid[nx][ny] == CELL_WALL || game->grid[nx][ny] == CELL_OBSTACLE) {
                    game->grid[nx][ny] = CELL_EMPTY;
                }
            }
        }
    }
    if (free_use) p->free_ability = FREE_NONE;
    else p->points -= COST_BOMB;
    return 1;
}

void shrink_board(GameState *game, int client1, int client2) {
    int offset = (game->current_cycle / SHRINK_INTERVAL) - 1;
    if (offset < 0 || offset >= BOARD_SIZE / 2) return;
    char msg[] = "CAUTION: The arena is shrinking!\n";
    send_all(client1, msg);
    send_all(client2, msg);
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (i == offset || i == BOARD_SIZE - 1 - offset || j == offset || j == BOARD_SIZE - 1 - offset) {
                if (game->grid[i][j] == CELL_P1 || game->grid[i][j] == CELL_P2) {
                    char kill_msg[] = "OVER:You were crushed by the shrinking arena!\n";
                    send_all(client1, kill_msg);
                    send_all(client2, kill_msg);
                }
                game->grid[i][j] = CELL_OBSTACLE;
            }
        }
    }
}
