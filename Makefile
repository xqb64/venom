SRC := $(wildcard src/*.c)

CFLAGS += -Wshadow -Wall -Wextra -Werror
CFLAGS += -Wswitch-default
CFLAGS += -Wredundant-decls
CFLAGS += -Wno-unused-parameter
CFLAGS += -Wformat-security
CFLAGS += -Wunreachable-code
CFLAGS += -O3
CFLAGS += -g3
LDLIBS = -lm

ifeq ($(debug), all)
	CFLAGS += -Dvenom_debug_tokenizer
	CFLAGS += -Dvenom_debug_parser
	CFLAGS += -Dvenom_debug_compiler
	CFLAGS += -Dvenom_debug_vm
	CFLAGS += -Dvenom_debug_disassembler
	CFLAGS += -g
endif

ifeq (sym, $(findstring sym, $(debug)))
	CFLAGS += -g
endif

ifeq (tokenizer, $(findstring tokenizer, $(debug)))
	CFLAGS += -Dvenom_debug_tokenizer
endif

ifeq (parser, $(findstring parser, $(debug)))
	CFLAGS += -Dvenom_debug_parser
endif

ifeq (compiler, $(findstring compiler, $(debug)))
	CFLAGS += -Dvenom_debug_compiler
endif

ifeq (vm, $(findstring vm, $(debug)))
	CFLAGS += -Dvenom_debug_vm
endif

ifeq (disassembler, $(findstring disassembler, $(debug)))
	CFLAGS += -Dvenom_debug_disassembler
endif

obj/%.o: src/%.c $(wildcard src/*.h)
	mkdir -vp obj && $(CC) -c $(CFLAGS) $< -o $@

all: venom

venom: $(SRC:src/%.c=obj/%.o)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm -rvf obj venom
	rm -f graph.gv graph.png callgrind.out

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


ifndef VIRTUAL_ENV
HAS_VENV := no
else
HAS_VENV := yes
endif

.PHONY: test

test:
	@if [ "$(HAS_VENV)" = "yes" ]; then \
		python -m pytest -n$$(nproc); \
	else \
		echo "Error: Virtual environment is not active. Activate it using 'source <your_env>/bin/activate' and then run 'make test'"; \
		exit 1; \
	fi

format:
	clang-format src/*.c src/*.h -style=file:.clang-format -i