# Quick build and test with gcc or clang (Linux, macOS). The Windows build is
# premake5 + Visual Studio (see premake5.lua); this file only builds the
# library, the tests and the program into build/make/.
#
#	make test               build and run every test group
#	make test T=eval        run only the group "eval" (T="eval cli" for more)
#	make groups             list the names of the groups
#	make matlog             build the program: build/make/bin/matlog

CC     ?= gcc
CFLAGS ?= -O1 -g
CFLAGS += -std=gnu11 -Wall -Wextra -Wno-comment -Werror -Iinclude -MMD -MP
OUT    := build/make
LIB    := $(shell find source/matlog -name "*.c" | sort)
LIB    := $(filter-out source/matlog/platform/win32/%,$(LIB))
LIBOBJ := $(patsubst source/%.c,$(OUT)/%.o,$(LIB))
TESTS  := $(wildcard source/tests/*.c)
TSTOBJ := $(patsubst source/%.c,$(OUT)/%.o,$(TESTS))
APPOBJ := $(patsubst source/%.c,$(OUT)/%.o,$(wildcard source/app/*.c))

.PHONY: test groups matlog clean

test: $(OUT)/bin/tests
	$(OUT)/bin/tests $(T)

groups: $(OUT)/bin/tests
	@grep -o '\.name = "[a-z_0-9]*"' source/tests/main.c | cut -d'"' -f2

matlog: $(OUT)/bin/matlog

$(OUT)/bin/tests: $(TSTOBJ) $(LIBOBJ)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ -lm -o $@

$(OUT)/bin/matlog: $(APPOBJ) $(LIBOBJ)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ -lm -o $@

$(OUT)/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OUT)

-include $(LIBOBJ:.o=.d) $(TSTOBJ:.o=.d) $(APPOBJ:.o=.d)
