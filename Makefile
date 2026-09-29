# ==========================
# Toolchain
# ==========================
CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -Iinclude
LDFLAGS :=

# ==========================
# Directories
# ==========================
SRC_DIR     := src
CLIENT_DIR  := client
TEST_DIR    := tests
BUILD_DIR   := build

# ==========================
# Install paths
# ==========================
PREFIX ?= /usr
BINDIR := $(PREFIX)/bin

# ==========================
# Binaries
# ==========================
SERVER_BIN := ramora-server
CLIENT_BIN := ramora-client
TEST_BIN   := ramora-test
SUITE_BIN  := ramora-suite

# ==========================
# Sources
# ==========================
SERVER_SRCS := \
	$(SRC_DIR)/server.c \
	$(SRC_DIR)/buf.c \
	$(SRC_DIR)/cmd_proc.c \
	$(SRC_DIR)/config.c \
	$(SRC_DIR)/conn.c \
	$(SRC_DIR)/g_data.c \
	$(SRC_DIR)/heap.c \
	$(SRC_DIR)/hmap.c \
	$(SRC_DIR)/logging_helper.c \
	$(SRC_DIR)/oper.c \
	$(SRC_DIR)/response.c \
	$(SRC_DIR)/timer.c \
	$(SRC_DIR)/vtr.c

CLIENT_SRCS := \
	$(CLIENT_DIR)/client.c

TEST_SRCS := \
	$(TEST_DIR)/test.c

SUITE_SRCS := \
	$(TEST_DIR)/test_suite.c

# ==========================
# Objects
# ==========================
SERVER_OBJS := $(SERVER_SRCS:%.c=$(BUILD_DIR)/%.o)
CLIENT_OBJS := $(CLIENT_SRCS:%.c=$(BUILD_DIR)/%.o)
TEST_OBJS   := $(TEST_SRCS:%.c=$(BUILD_DIR)/%.o)
SUITE_OBJS  := $(SUITE_SRCS:%.c=$(BUILD_DIR)/%.o)

# ==========================
# Default target
# ==========================
.PHONY: all
all: $(SERVER_BIN) $(CLIENT_BIN) $(TEST_BIN) $(SUITE_BIN)

# ==========================
# Compile rules
# ==========================
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ==========================
# Link rules
# ==========================
$(SERVER_BIN): $(SERVER_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(TEST_BIN): $(TEST_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(SUITE_BIN): $(SUITE_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

# ==========================
# Test targets
# ==========================
.PHONY: test
test: all
	./$(SUITE_BIN)

.PHONY: asan
asan: clean
	$(MAKE) CFLAGS="-Wall -Wextra -O1 -g -fsanitize=address,undefined -Iinclude" LDFLAGS="-fsanitize=address,undefined" all
	./$(SUITE_BIN)

# ==========================
# Cleanup
# ==========================
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(SERVER_BIN) $(CLIENT_BIN) $(TEST_BIN) $(SUITE_BIN)
	rm -f *.rdb *.log
