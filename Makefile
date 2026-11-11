CC = gcc
CFLAGS = -Wall -Iinclude

all: gridlock_server gridlock_client

gridlock_server: src/server_main.c src/game_logic.c src/network.c
	$(CC) $(CFLAGS) -o $@ $^

gridlock_client: src/client_main.c src/network.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f gridlock_server gridlock_client

.PHONY: all clean
