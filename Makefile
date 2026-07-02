#==============================================================================
# Makefile — ECUS: Embedded CAN-UDS Diagnostic Stack
#
# Targets:
#   make            Build the release binary (build/ecus)
#   make debug      Build with -g -O0 and ASan/UBSan instrumentation
#   make test       Build and run the unit test suite
#   make clean      Remove all build artifacts
#   make lint       Run cppcheck static analysis (MISRA-adjacent checks)
#   make docs       Generate Doxygen API documentation
#   make install    Install the binary to /usr/local/bin (requires sudo)
#
# Toolchain: GCC >= 9 or Clang >= 10, C11, POSIX threads.
#==============================================================================

CC          := gcc
STD         := -std=c11
TARGET      := ecus
BUILD_DIR   := build
SRC_DIR     := src
INC_DIR     := include
TEST_DIR    := tests

#------------------------------------------------------------------------------
# Source discovery — recursively find all .c files under src/
#------------------------------------------------------------------------------
SRCS        := $(shell find $(SRC_DIR) -name '*.c')
OBJS        := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS        := $(OBJS:.o=.d)

#------------------------------------------------------------------------------
# Include paths — every subdirectory under include/ is on the search path
#------------------------------------------------------------------------------
INCLUDES    := -I$(INC_DIR)

#------------------------------------------------------------------------------
# Warning flags — aggressive, MISRA-adjacent strictness
#------------------------------------------------------------------------------
WARN_FLAGS  := -Wall -Wextra -Wshadow -Wconversion \
               -Wsign-conversion -Wcast-qual -Wstrict-prototypes \
               -Wmissing-prototypes -Wpointer-arith -Wwrite-strings \
               -Wredundant-decls -Wundef -Wformat=2 -Wnull-dereference

#------------------------------------------------------------------------------
# Feature macros
#   ECUS_LOG_COLOR        : enable ANSI colour codes in Logger (opt-in)
#   _POSIX_C_SOURCE        : required for clock_gettime, nanosleep, strtok_r
#------------------------------------------------------------------------------
FEATURE_FLAGS := -D_POSIX_C_SOURCE=200809L

#------------------------------------------------------------------------------
# Build mode flags
#------------------------------------------------------------------------------
RELEASE_FLAGS  := -O2 -DNDEBUG
DEBUG_FLAGS    := -O0 -g3 -DDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer

CFLAGS      := $(STD) $(WARN_FLAGS) $(INCLUDES) $(FEATURE_FLAGS) -MMD -MP
LDFLAGS     := -pthread
LDLIBS      :=

# Default to release build.
CFLAGS      += $(RELEASE_FLAGS)

#------------------------------------------------------------------------------
# Default target
#------------------------------------------------------------------------------
.PHONY: all
all: clean $(BUILD_DIR)/$(TARGET)
	@echo ""
	@echo "  ✓ Build complete: $(BUILD_DIR)/$(TARGET)"
	@echo "  Run with: ./$(BUILD_DIR)/$(TARGET)"
	@echo ""

$(BUILD_DIR)/$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

# Pattern rule: compile each .c into build/ mirroring src/ subdirectories.
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

-include $(DEPS)

#------------------------------------------------------------------------------
# Debug build — ASan + UBSan instrumented
#------------------------------------------------------------------------------
.PHONY: debug
debug: CFLAGS := $(STD) $(WARN_FLAGS) $(INCLUDES) $(FEATURE_FLAGS) -MMD -MP $(DEBUG_FLAGS)
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean $(BUILD_DIR)/$(TARGET)
	@echo "  ✓ Debug build complete (ASan/UBSan enabled)"

#------------------------------------------------------------------------------
# Unit tests
#------------------------------------------------------------------------------
TEST_SRCS   := $(shell find $(TEST_DIR) -name '*.c')
TEST_BIN    := $(BUILD_DIR)/run_tests

# Test build excludes Main.c (tests provide their own entry point).
LIB_SRCS    := $(filter-out $(SRC_DIR)/app/Main.c,$(SRCS))
LIB_OBJS    := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))
TEST_OBJS   := $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/tests/%.o,$(TEST_SRCS))

$(BUILD_DIR)/tests/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(TEST_DIR)/framework -I$(TEST_DIR) -c $< -o $@

$(TEST_BIN): $(LIB_OBJS) $(TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LIB_OBJS) $(TEST_OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

.PHONY: test
test: clean $(TEST_BIN)
	@echo ""
	@echo "  Running ECUS unit test suite..."
	@echo "  ─────────────────────────────────"
	@./$(TEST_BIN)

#------------------------------------------------------------------------------
# Static analysis (cppcheck — MISRA-adjacent checks)
#------------------------------------------------------------------------------
.PHONY: lint
lint:
	@command -v cppcheck >/dev/null 2>&1 && \
	cppcheck --enable=all --std=c11 --inline-suppr \
	         --suppress=missingIncludeSystem \
	         --suppress=unusedFunction \
	         -I$(INC_DIR) $(SRC_DIR) || \
	echo "cppcheck not installed — skipping static analysis"

#------------------------------------------------------------------------------
# Documentation (Doxygen)
#------------------------------------------------------------------------------
.PHONY: docs
docs:
	@command -v doxygen >/dev/null 2>&1 && doxygen Doxyfile || \
	echo "doxygen not installed — skipping documentation generation"

#------------------------------------------------------------------------------
# Installation
#------------------------------------------------------------------------------
.PHONY: install
install: all
	install -m 0755 $(BUILD_DIR)/$(TARGET) /usr/local/bin/$(TARGET)
	@echo "  ✓ Installed to /usr/local/bin/$(TARGET)"

#------------------------------------------------------------------------------
# Housekeeping
#------------------------------------------------------------------------------
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

.PHONY: help
help:
	@echo "ECUS Makefile targets:"
	@echo "  make            Build release binary"
	@echo "  make debug      Build with ASan/UBSan instrumentation"
	@echo "  make test       Build and run unit tests"
	@echo "  make lint       Run static analysis (cppcheck)"
	@echo "  make docs       Generate Doxygen documentation"
	@echo "  make clean      Remove all build artifacts"
	@echo "  make install    Install binary to /usr/local/bin"
