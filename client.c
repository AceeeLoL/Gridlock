#include <stdio.h>
#include <stdbool.h>

#define BOARD_SIZE 15

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

/* Function stubs */
void init_game(GameState *game);
void print_board(const GameState *game);
bool is_valid_cell(const GameState *game, Position p);
bool move_player(GameState *game, Player *p, char dir);

int main(void) {
    GameState game;
    
    // TODO: Initialize game state
    // TODO: Implement main game loop and basic mechanics

    return 0;
}
