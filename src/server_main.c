#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "game_logic.h"
#include "network.h"

static void send_board_to_client(int fd, const GameState *game) {
    char board_buf[2048];
    int len = snprintf(board_buf, sizeof(board_buf), "\n--- GRIDLOCK (Cycle %d) ---\n  ", game->current_cycle);
    for (int j = 0; j < BOARD_SIZE; j++) {
        len += snprintf(board_buf + len, sizeof(board_buf) - len, "%d ", j % 10);
    }
    len += snprintf(board_buf + len, sizeof(board_buf) - len, "\n");
    for (int i = 0; i < BOARD_SIZE; i++) {
        len += snprintf(board_buf + len, sizeof(board_buf) - len, "%d ", i % 10);
        for (int j = 0; j < BOARD_SIZE; j++) {
            len += snprintf(board_buf + len, sizeof(board_buf) - len, "%c ", game->grid[i][j]);
        }
        len += snprintf(board_buf + len, sizeof(board_buf) - len, "\n");
    }
    send_all(fd, board_buf);
}

int main(int argc, char *argv[]) {
    srand((unsigned int)time(NULL));

    if (argc < 2) {
        printf("Usage: %s port_no\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(server_fd, 2) < 0) { perror("listen"); return 1; }

    signal(SIGPIPE, SIG_IGN);
    printf("Gridlock Server listening on port %d...\n", port);

    while (1) {
        int clients[2];
        for (int i = 0; i < 2; i++) {
            clients[i] = accept(server_fd, NULL, NULL);
            char msg[64];
            snprintf(msg, sizeof(msg), "CONNECTED:Welcome Player %d! Waiting for opponent...\n", i + 1);
            send_all(clients[i], msg);
        }

        GameState game;
        init_game(&game);
        generate_obstacles(&game, 15);
        send_all(clients[0], "START:Game Start! You are Player 1 (Left side).\n");
        send_all(clients[1], "START:Game Start! You are Player 2 (Right side).\n");

        int active = 0;
        while (1) {
            Player *player = (active == 0) ? &game.p1 : &game.p2;
            Player *other = (active == 0) ? &game.p2 : &game.p1;
            int waiting = 1 - active;

            /* Render board layout to BOTH players */
            send_board_to_client(clients[0], &game);
            send_board_to_client(clients[1], &game);

            /* Inform the inactive player to wait */
            char wait_msg[64];
            snprintf(wait_msg, sizeof(wait_msg), "WAIT:Waiting for Player %d to move...\n", player->id);
            send_all(clients[waiting], wait_msg);

            /* Request move input from the active player */
            char state[1024];
            snprintf(state, sizeof(state), "STATE:Cycle %d | Your Points: %d | Free: %s | Pos: (%d,%d)\n", 
                     game.current_cycle, player->points, free_ability_name(player->free_ability), player->pos.x, player->pos.y);
            send_all(clients[active], state);
            send_all(clients[active], "INPUT\n");

            char buffer[256];
            if (receive_line(clients[active], buffer, sizeof(buffer)) <= 0) break;

            int action = -1;
            char dir = ' ';
            sscanf(buffer, "%d %c", &action, &dir);

            int valid = 0;
            if (action == 0) valid = move_player(&game, player, dir);
            else if (action == 1) valid = ability_sprint(&game, player, dir);
            else if (action == 2) valid = ability_jump(&game, player, dir);
            else if (action == 3) valid = ability_bomb(&game, player);

            if (valid == 0) {
                send_all(clients[active], "INVALID:Not enough points or invalid move. Try again.\n");
                continue;
            } else if (valid == -1) {
                char msg[128];
                snprintf(msg, sizeof(msg), "OVER:Player %d crashed into an obstacle! Player %d wins!\n", player->id, other->id);
                send_all(clients[0], msg);
                send_all(clients[1], msg);
                break;
            }

            if (player->last_reward != REWARD_NONE) {
                char reward_msg[160];
                if (player->last_reward == REWARD_POINTS) snprintf(reward_msg, sizeof(reward_msg), "Mystery Block: +2 points!\n");
                else if (player->last_reward == REWARD_FREE) snprintf(reward_msg, sizeof(reward_msg), "Mystery Block: a free %s!\n", free_ability_name(player->free_ability));
                else snprintf(reward_msg, sizeof(reward_msg), "Mystery Block: reward lost.\n");
                send_all(clients[active], reward_msg);
                player->last_reward = REWARD_NONE;
            }

            active = 1 - active;
            if (active == 0) {
                game.current_cycle++;
                if (game.p1.points < MAX_POINTS) game.p1.points++;
                if (game.p2.points < MAX_POINTS) game.p2.points++;
                if (game.current_cycle % MYSTERY_INTERVAL == 0) spawn_mystery_block(&game);
                if (game.current_cycle % SHRINK_INTERVAL == 0) shrink_board(&game, clients[0], clients[1]);
            }
            print_board(&game);
        }
        for (int i = 0; i < 2; i++) close(clients[i]);
    }
    close(server_fd);
    return 0;
}
