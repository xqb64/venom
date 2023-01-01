CC = gcc
CFLAGS += -g
CFLAGS += -Wshadow -Wall -Wextra
CFLAGS += -O3
LDLIBS = -lm

ifdef $(debug)
	CFLAGS += -Dvenom_debug
endif

venom:
	$(CC) $(CFLAGS) $(wildcard ./src/*.c) $(LDLIBS)