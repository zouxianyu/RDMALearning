CC=gcc
CFLAGS=

all:clean client server

client: client.c
	$(CC) $(CFLAGS) $< -o $@

server: server.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf ./client ./server