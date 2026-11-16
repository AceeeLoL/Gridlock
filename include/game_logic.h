#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <stdbool.h>
#include "game_state.h"

typedef enum {
    SHRINK_NO_DEATH,
    SHRINK_PLAYER_1_DIED,
    SHRINK_PLAYER_2_DIED,
    SHRINK_BOTH_DIED
} ShrinkResult;

void init_game(GameState *game);
void generate_obstacles(GameState *game, int num_obstacles);
void print_board(const GameState *game);
bool is_valid_cell(const GameState *game, Position p);
void spawn_mystery_block(GameState *game);
const char *free_ability_name(int ability);

int move_player(GameState *game, Player *p, char dir);
int ability_sprint(GameState *game, Player *p, char dir);
int ability_jump(GameState *game, Player *p, char dir);
int ability_bomb(GameState *game, Player *p);

ShrinkResult shrink_board(GameState *game, int client1, int client2);

#endif
