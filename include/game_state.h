#ifndef GAME_STATE_H
#define GAME_STATE_H

#include "constants.h"

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

#endif
