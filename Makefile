# Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2

all: client server

client: client.c
	$(CC) $(CFLAGS) -o client client.c

server: server.c
	$(CC) $(CFLAGS) -o server server.c -pthread

clean:
	rm -f client server