CC := $(CROSS_COMPILE)gcc
OUT ?= out
CPPFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -Isrc
CFLAGS ?= -O2 -g -std=c99 -Wall -Wextra -Werror

.PHONY: all host
all: $(OUT)/file_server $(OUT)/file_client

$(OUT)/file_server: $(OUT)/server.o $(OUT)/common.o

$(OUT)/file_client: $(OUT)/client.o $(OUT)/common.o

$(OUT)/file_server $(OUT)/file_client:
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OUT)/%.o: src/%.c src/common.h Makefile
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

host:
	$(MAKE) CROSS_COMPILE= OUT=out/host all

-include $(wildcard $(OUT)/*.d)
