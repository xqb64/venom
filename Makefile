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
	rm graph.gv graph.png callgrind.out

# profiling stuff
#
#	$ sudo apt install valgrind
#	$ python3 -m pip install gprof2dot
#	$ make graph.png

callgrind.out: venom
	valgrind --tool=callgrind --callgrind-out-file=$@ ./$< examples/example02.vnm

graph.gv: callgrind.out
	gprof2dot $< --format=callgrind --output=$@

graph.png: graph.gv
	dot -Tpng $< -o $@