#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
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

int main(void) {
    GameState game;
    init_game(&game);
    
    // Generate 10 random obstacles
    generate_obstacles(&game, 10); 
    
    print_board(&game);
    
    // TODO: Implement special actions (Sprint, Jump, Bomb)
    // TODO: Add TCP socket server networking
    // TODO: Implement turn loop & win/draw condition checks

    return 0;
}