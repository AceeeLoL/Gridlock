#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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

        int connected = 1;
        while (connected) {
            int player1_wins = 0;
            int player2_wins = 0;

            for (int round = 1; round <= MAX_ROUNDS && connected; round++) {
                GameState game;
                init_game(&game);
                generate_obstacles(&game, 15);

                char start_msg[128];
                snprintf(start_msg, sizeof(start_msg),
                         "START:Round %d of %d. You are Player 1 (Left side).\n",
                         round, MAX_ROUNDS);
                send_all(clients[0], start_msg);
                snprintf(start_msg, sizeof(start_msg),
                         "START:Round %d of %d. You are Player 2 (Right side).\n",
                         round, MAX_ROUNDS);
                send_all(clients[1], start_msg);

                int active = 0;
                int winner = 0;
                int round_over = 0;
                char round_reason[128] = "The round ended in a draw.";
                while (!round_over) {
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
                             game.current_cycle, player->points, free_ability_name(player->free_ability),
                             player->pos.x, player->pos.y);
                    send_all(clients[active], state);
                    send_all(clients[active], "INPUT\n");

                    char buffer[256];
                    if (receive_line(clients[active], buffer, sizeof(buffer)) <= 0) {
                        connected = 0;
                        break;
                    }

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
                        winner = other->id;
                        snprintf(round_reason, sizeof(round_reason),
                                 "Player %d crashed into an obstacle, trail, or opponent.", player->id);
                        round_over = 1;
                        continue;
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
                    ShrinkResult shrink_result = SHRINK_NO_DEATH;
                    if (active == 0) {
                        game.current_cycle++;
                        if (game.p1.points < MAX_POINTS) game.p1.points++;
                        if (game.p2.points < MAX_POINTS) game.p2.points++;
                        if (game.current_cycle % MYSTERY_INTERVAL == 0) spawn_mystery_block(&game);
                        if (game.current_cycle % SHRINK_INTERVAL == 0) {
                            shrink_result = shrink_board(&game, clients[0], clients[1]);
                        }
                    }
                    print_board(&game);
                    if (shrink_result != SHRINK_NO_DEATH) {
                        if (shrink_result == SHRINK_PLAYER_1_DIED) winner = 2;
                        else if (shrink_result == SHRINK_PLAYER_2_DIED) winner = 1;
                        if (shrink_result == SHRINK_BOTH_DIED) {
                            snprintf(round_reason, sizeof(round_reason),
                                     "Both players were crushed by the shrinking arena.");
                        } else {
                            snprintf(round_reason, sizeof(round_reason),
                                     "Player %d was crushed by the shrinking arena.",
                                     shrink_result == SHRINK_PLAYER_1_DIED ? 1 : 2);
                        }
                        round_over = 1;
                    }
                }

                if (!connected) break;
                if (winner == 1) player1_wins++;
                else if (winner == 2) player2_wins++;
                char result_msg[256];
                if (winner != 0) {
                    snprintf(result_msg, sizeof(result_msg),
                             "ROUND_OVER:Round %d/%d: %s Player %d wins. Match score: %d-%d.\n",
                             round, MAX_ROUNDS, round_reason, winner, player1_wins, player2_wins);
                } else {
                    snprintf(result_msg, sizeof(result_msg),
                             "ROUND_OVER:Round %d/%d: %s Match score: %d-%d.\n",
                             round, MAX_ROUNDS, round_reason, player1_wins, player2_wins);
                }
                send_all(clients[0], result_msg);
                send_all(clients[1], result_msg);
            }

            if (connected) {
                char match_msg[160];
                if (player1_wins > player2_wins) {
                    snprintf(match_msg, sizeof(match_msg), "Match winner: Player 1 (%d-%d).",
                             player1_wins, player2_wins);
                } else if (player2_wins > player1_wins) {
                    snprintf(match_msg, sizeof(match_msg), "Match winner: Player 2 (%d-%d).",
                             player2_wins, player1_wins);
                } else {
                    snprintf(match_msg, sizeof(match_msg), "The match is a draw (%d-%d).",
                             player1_wins, player2_wins);
                }
                char result_msg[192];
                snprintf(result_msg, sizeof(result_msg), "MATCH_RESULT:%s\n", match_msg);
                send_all(clients[0], result_msg);
                send_all(clients[1], result_msg);
                send_all(clients[0],
                         "RESTART_PROMPT:Play another 3-round match with the same players? (y/n)\n");
                send_all(clients[1],
                         "RESTART_PROMPT:Play another 3-round match with the same players? (y/n)\n");

                char answers[2][32];
                if (receive_line(clients[0], answers[0], sizeof(answers[0])) <= 0 ||
                    receive_line(clients[1], answers[1], sizeof(answers[1])) <= 0) {
                    connected = 0;
                } else if (tolower((unsigned char)answers[0][0]) != 'y' ||
                           tolower((unsigned char)answers[1][0]) != 'y') {
                    send_all(clients[0], "OVER:Match finished. Thanks for playing!\n");
                    send_all(clients[1], "OVER:Match finished. Thanks for playing!\n");
                    connected = 0;
                }
            }
        }
        for (int i = 0; i < 2; i++) close(clients[i]);
    }
    close(server_fd);
    return 0;
}
