CC = gcc
CFLAGS = -g -Wshadow -Wall -Wextra -O3
LDLIBS = -lm

venom:
	$(CC) $(CFLAGS) $(wildcard ./src/*.c) $(LDLIBS)