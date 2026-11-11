CC = gcc
CFLAGS = -Wall -Iinclude
LDFLAGS = 

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj

SERVER_SRC = $(SRC_DIR)/server_main.c $(SRC_DIR)/game_logic.c $(SRC_DIR)/network.c
CLIENT_SRC = $(SRC_DIR)/client_main.c $(SRC_DIR)/network.c

SERVER_OBJ = $(SERVER_SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
CLIENT_OBJ = $(CLIENT_SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

TARGET_SERVER = gridlock_server
TARGET_CLIENT = gridlock_client

all: $(OBJ_DIR) $(TARGET_SERVER) $(TARGET_CLIENT)

$(TARGET_SERVER): $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_CLIENT): $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET_SERVER) $(TARGET_CLIENT)

.PHONY: all clean
