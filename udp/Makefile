CC=gcc
all: client server

client: client.c
	$(CC) -o client client.c -lpthread

server: server.c
	$(CC) -o server server.c -lpthread -lncurses

clean:
	rm -f client server
