SRC := $(wildcard src/*.c)

CFLAGS += -g
CFLAGS += -Wshadow -Wall -Wextra
CFLAGS += -Wno-unused-parameter
CFLAGS += -O3
LDLIBS = -lm

ifeq ($(debug), 1)
	CFLAGS += -Dvenom_debug
endif

obj/%.o: src/%.c $(wildcard src/*.h)
	mkdir -vp obj && $(CC) -c $(CFLAGS) $< -o $@

all: venom

venom: $(SRC:src/%.c=obj/%.o)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm -rvf obj venom