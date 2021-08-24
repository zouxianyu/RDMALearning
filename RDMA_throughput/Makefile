CC=gcc
CFLAGS=-g

all:clean client server 

client: client.c
	$(CC) $(CFLAGS) $< -o $@

server: server.c
	$(CC) $(CFLAGS) $< -o $@

template: bw_template.c
	$(CC) $(CFLAGS) $< -libverbs -o server && cp server client

clean:
	rm -rf ./client ./server
