# Makefile for building Client and Server file
# By Daniel Pretlove (N10193308), Jordan Auld (N10227750), Justin Vickers (N9705872)

CC=gcc
CFLAGS= -lpthread

all: 
	$(CC) -o Server Server.c $(CFLAGS)
	$(CC) -o Client Client.c $(CFLAGS)