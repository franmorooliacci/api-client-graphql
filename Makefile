CC = gcc
CFLAGS = -Wall -Wextra -I./include -g
LDFLAGS = -lmicrohttpd -lcurl -lsqlite3 -lcjson

SRC = src/main.c src/db.c src/api.c src/github.c
OBJ = $(SRC:.c=.o)
EXEC = github_worker

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(EXEC)

.PHONY: all clean
