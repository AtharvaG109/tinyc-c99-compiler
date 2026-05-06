CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wno-unused-parameter -g -Iinclude
LDFLAGS =

SRCDIR = src
TESTDIR = tests
BUILDDIR = build
SELFHOST_DIR ?= /private/tmp/tinyc-selfhost-build
SELFHOST_OUT ?= /private/tmp/tinyc-selfhost

SRCS = $(SRCDIR)/arena.c $(SRCDIR)/strtab.c $(SRCDIR)/diagnostic.c \
       $(SRCDIR)/lexer.c $(SRCDIR)/type.c $(SRCDIR)/parser.c \
       $(SRCDIR)/sema.c $(SRCDIR)/ir.c $(SRCDIR)/irgen.c \
       $(SRCDIR)/regalloc.c $(SRCDIR)/codegen.c $(SRCDIR)/abi_classify.c
MAIN_SRC = $(SRCDIR)/main.c

.PHONY: all build lint clean check test test_arena test_strtab test_lexer test_parser test_integration test_abi selfhost

all: $(BUILDDIR)/tinyc

build: $(BUILDDIR)/tinyc

lint:
	$(CC) $(CFLAGS) -Werror -fsyntax-only $(SRCS) $(MAIN_SRC)

$(BUILDDIR)/.dir:
	mkdir -p $(BUILDDIR)
	touch $@

$(BUILDDIR)/tinyc: $(SRCS) $(MAIN_SRC) | $(BUILDDIR)/.dir
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(MAIN_SRC)

$(BUILDDIR)/test_arena: $(TESTDIR)/unit/test_arena.c $(SRCS) | $(BUILDDIR)/.dir
	$(CC) $(CFLAGS) -I$(TESTDIR) -o $@ $(TESTDIR)/unit/test_arena.c $(SRCS)

$(BUILDDIR)/test_strtab: $(TESTDIR)/unit/test_strtab.c $(SRCS) | $(BUILDDIR)/.dir
	$(CC) $(CFLAGS) -I$(TESTDIR) -o $@ $(TESTDIR)/unit/test_strtab.c $(SRCS)

$(BUILDDIR)/test_lexer: $(TESTDIR)/unit/test_lexer.c $(SRCS) | $(BUILDDIR)/.dir
	$(CC) $(CFLAGS) -I$(TESTDIR) -o $@ $(TESTDIR)/unit/test_lexer.c $(SRCS)

$(BUILDDIR)/test_parser: $(TESTDIR)/unit/test_parser.c $(SRCS) | $(BUILDDIR)/.dir
	$(CC) $(CFLAGS) -I$(TESTDIR) -o $@ $(TESTDIR)/unit/test_parser.c $(SRCS)

test: $(BUILDDIR)/test_arena $(BUILDDIR)/test_strtab $(BUILDDIR)/test_lexer $(BUILDDIR)/test_parser
	@echo ""; echo "=== Running unit tests ==="; echo ""
	$(BUILDDIR)/test_arena
	$(BUILDDIR)/test_strtab
	$(BUILDDIR)/test_lexer
	$(BUILDDIR)/test_parser

check: test test_integration test_abi

selfhost: $(BUILDDIR)/tinyc
	@echo ""; echo "=== Building tinyc with tinyc ==="; echo ""
	@mkdir -p $(SELFHOST_DIR)
	@objs=""; \
	for src in $(SRCS) $(MAIN_SRC); do \
	    name=$$(basename $$src .c); \
	    pp="$(SELFHOST_DIR)/$$name.i"; \
	    asm="$(SELFHOST_DIR)/$$name.s"; \
	    obj="$(SELFHOST_DIR)/$$name.o"; \
	    echo "  TINYC $$src"; \
	    $(CC) -E -P -std=c99 -DTINYC_SELFHOST -nostdinc -Iselfhost/include -Iinclude $$src -o $$pp && \
	    $(BUILDDIR)/tinyc -S $$pp -o $$asm && \
	    $(CC) -c $$asm -o $$obj || exit 1; \
	    objs="$$objs $$obj"; \
	done; \
	$(CC) $(LDFLAGS) $$objs -o $(SELFHOST_OUT)
	@echo "Built: $(SELFHOST_OUT)"

test_integration: $(BUILDDIR)/tinyc
	@echo ""; echo "=== Running integration tests ==="; echo ""
	@ok=0; fail=0; \
	for src in $(TESTDIR)/integration/*.c; do \
	    name=$$(basename $$src .c); \
	    $(BUILDDIR)/tinyc -S $$src -o /tmp/$$name.s 2>/dev/null && \
	    as /tmp/$$name.s -o /tmp/$$name.o 2>/dev/null && \
	    $(CC) /tmp/$$name.o -o /tmp/$$name 2>/dev/null && \
	    /tmp/$$name > /dev/null 2>&1 && ok=$$((ok+1)) && echo "  PASS  $$name" || \
	    { fail=$$((fail+1)); echo "  FAIL  $$name"; }; \
	done; \
	echo ""; echo "$$ok passed, $$fail failed"; \
	test $$fail -eq 0

test_abi: $(BUILDDIR)/tinyc
	@echo ""; echo "=== Running ABI tests ==="; echo ""
	@ok=0; fail=0; \
	for src in $(TESTDIR)/abi/*.c; do \
	    name=$$(basename $$src .c); \
	    $(BUILDDIR)/tinyc -S $$src -o /tmp/$$name.s 2>/dev/null && \
	    as /tmp/$$name.s -o /tmp/$$name.o 2>/dev/null && \
	    $(CC) /tmp/$$name.o -o /tmp/$$name 2>/dev/null && \
	    /tmp/$$name > /dev/null 2>&1 && ok=$$((ok+1)) && echo "  PASS  $$name" || \
	    { fail=$$((fail+1)); echo "  FAIL  $$name"; }; \
	done; \
	echo ""; echo "$$ok passed, $$fail failed"; \
	test $$fail -eq 0

clean:
	rm -rf $(BUILDDIR)
