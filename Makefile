PROJECT := kitty-terminal-session
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=

CC ?= cc
AR ?= ar
INSTALL ?= install

KITTY_FRAMEBUFFER_DIR ?= third_party/kitty-framebuffer
KITTY_INPUT_DIR ?= third_party/kitty-input
KITTY_KEYBOARD_DIR ?= $(KITTY_INPUT_DIR)/third_party/kitty_keyboard

CPPFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -Iinclude \
	-I$(KITTY_FRAMEBUFFER_DIR)/include -I$(KITTY_INPUT_DIR)/include \
	-I$(KITTY_KEYBOARD_DIR)/include
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Werror
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -fPIC -pthread $(WARNINGS)
DEPFLAGS := -MMD -MP
LDLIBS += -lz -pthread

LIB := $(BUILD_DIR)/lib$(PROJECT).a
OBJECTS := $(BUILD_DIR)/kitty_terminal_session.o \
	$(BUILD_DIR)/kitty_framebuffer.o $(BUILD_DIR)/kitty_input.o \
	$(BUILD_DIR)/kitty_input_posix.o $(BUILD_DIR)/kitty_keyboard.o \
	$(BUILD_DIR)/kitty_keyboard_posix.o
DEPS := $(OBJECTS:.o=.d)
TEST_BIN := $(BUILD_DIR)/test-session
BENCHMARK_BIN := $(BUILD_DIR)/benchmark-session
SANITIZE_BUILD_DIR ?= $(BUILD_DIR)/sanitize

all: $(LIB)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/kitty_terminal_session.o: src/kitty_terminal_session.c \
	include/kitty_terminal_session.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD_DIR)/kitty_framebuffer.o: $(KITTY_FRAMEBUFFER_DIR)/src/kitty_framebuffer.c \
	$(KITTY_FRAMEBUFFER_DIR)/include/kitty_framebuffer.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD_DIR)/kitty_input.o: $(KITTY_INPUT_DIR)/src/kitty_input.c \
	$(KITTY_INPUT_DIR)/include/kitty_input.h \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD_DIR)/kitty_input_posix.o: $(KITTY_INPUT_DIR)/src/kitty_input_posix.c \
	$(KITTY_INPUT_DIR)/include/kitty_input.h \
	$(KITTY_INPUT_DIR)/include/kitty_input_posix.h \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard_posix.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD_DIR)/kitty_keyboard.o: $(KITTY_KEYBOARD_DIR)/src/kitty_keyboard.c \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD_DIR)/kitty_keyboard_posix.o: \
	$(KITTY_KEYBOARD_DIR)/src/kitty_keyboard_posix.c \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard.h \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard_posix.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(LIB): $(OBJECTS)
	$(AR) rcs $@ $^

$(TEST_BIN): tests/test_session.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(LDLIBS) -lutil

$(BENCHMARK_BIN): benchmarks/benchmark_session.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(LDLIBS) -lutil

test: $(TEST_BIN)
	$(TEST_BIN)

benchmark: $(BENCHMARK_BIN)
	$(BENCHMARK_BIN)

sanitize:
	$(MAKE) BUILD_DIR=$(SANITIZE_BUILD_DIR) \
		CFLAGS='-O1 -g3 -fsanitize=address,undefined \
		-fno-omit-frame-pointer' test

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	$(INSTALL) -m 0644 include/kitty_terminal_session.h \
		$(KITTY_FRAMEBUFFER_DIR)/include/kitty_framebuffer.h \
		$(KITTY_INPUT_DIR)/include/kitty_input.h \
		$(KITTY_INPUT_DIR)/include/kitty_input_posix.h \
		$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard.h \
		$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard_posix.h \
		$(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)

.PHONY: all benchmark clean install sanitize test
