SRC := $(wildcard src/*.c)

CFLAGS += -g
CFLAGS += -Wshadow -Wall -Wextra
CFLAGS += -Wno-unused-parameter
CFLAGS += -O3
LDLIBS = -lm

ifeq ($(debug), all)
	CFLAGS += -Dvenom_debug_parser
	CFLAGS += -Dvenom_debug_compiler
	CFLAGS += -Dvenom_debug_vm
endif

ifeq ($(debug), parser)
	CFLAGS += -Dvenom_debug_parser
endif

ifeq ($(debug), disassembler)
	CFLAGS += -Dvenom_debug_disassembler
endif

ifeq ($(debug), vm)
	CFLAGS += -Dvenom_debug_vm
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
	valgrind --tool=callgrind --callgrind-out-file=$@ ./$< $(benchmark)

graph.gv: callgrind.out
	gprof2dot $< --format=callgrind --output=$@

graph.png: graph.gv
	dot -Tpng $< -o $@