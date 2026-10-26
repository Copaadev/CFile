# CFile - Professional CLI file manager
#
# Targets: all (default), clean, install, uninstall, test, sanitize

PROGRAM  := cfile
PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin
MANDIR   ?= $(PREFIX)/share/man

CC       ?= cc
CFLAGS   ?= -O2 -g
BUILD    ?= build

override CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Isrc
override CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
                    -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 \
                    -fstack-protector-strong

LIB_SRC  := src/utils.c src/files.c src/directory.c
APP_SRC  := $(LIB_SRC) src/interface.c src/main.c
LIB_OBJ  := $(LIB_SRC:src/%.c=$(BUILD)/%.o)
APP_OBJ  := $(APP_SRC:src/%.c=$(BUILD)/%.o)
TEST_BIN := $(BUILD)/test_cfile

.PHONY: all clean install uninstall test sanitize

all: $(BUILD)/$(PROGRAM)

$(BUILD)/$(PROGRAM): $(APP_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD):
	mkdir -p $@

$(TEST_BIN): tests/test_cfile.c $(LIB_OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

test: $(TEST_BIN) $(BUILD)/$(PROGRAM)
	$(TEST_BIN)
	sh tests/smoke.sh $(BUILD)/$(PROGRAM)

# Runs the whole test suite under AddressSanitizer and UBSan.
sanitize:
	$(MAKE) BUILD=build-asan CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
	        LDFLAGS="-fsanitize=address,undefined" test

install: all
	install -Dm755 $(BUILD)/$(PROGRAM) $(DESTDIR)$(BINDIR)/$(PROGRAM)
	install -Dm644 docs/cfile.1 $(DESTDIR)$(MANDIR)/man1/cfile.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(PROGRAM)
	rm -f $(DESTDIR)$(MANDIR)/man1/cfile.1

clean:
	rm -rf build build-asan

-include $(wildcard $(BUILD)/*.d)
