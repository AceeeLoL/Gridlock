#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <stdbool.h>
#include "game_state.h"

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

void shrink_board(GameState *game, int client1, int client2);

#endif
