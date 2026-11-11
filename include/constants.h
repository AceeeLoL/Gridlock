#ifndef CONSTANTS_H
#define CONSTANTS_H

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

#endif
