CC = gcc
CFLAGS = -g -Wshadow -Wall -Wextra -O3 -D venom_debug
LDLIBS = -lm

venom:
	$(CC) $(CFLAGS) $(wildcard ./src/*.c) $(LDLIBS)