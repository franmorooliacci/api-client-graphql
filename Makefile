CC = gcc
CFLAGS = -Wall -Wextra -I./include -g
LDFLAGS = -lmicrohttpd -lcurl -lsqlite3 -lcjson

SRV_SRC = src/main.c src/db.c src/api.c src/github.c
SRV_OBJ = $(SRV_SRC:.c=.o)
SRV_EXEC = github_server

CLI_SRC = src/cli.c
CLI_OBJ = $(CLI_SRC:.c=.o)
CLI_EXEC = github_client
CLI_LDFLAGS = -lcurl -lcjson -lsqlite3

all: $(SRV_EXEC) $(CLI_EXEC)

$(SRV_EXEC): $(SRV_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

$(CLI_EXEC): $(CLI_OBJ)
	$(CC) -o $@ $^ $(CLI_LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SRV_OBJ) $(CLI_OBJ) $(SRV_EXEC) $(CLI_EXEC)

.PHONY: all clean
