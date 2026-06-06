CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic \
         -Iinclude -Iinclude/core -Iinclude/ui -Iinclude/data \
         -Iinclude/net -Iinclude/widgets -Iinclude/utils -Iinclude/vendor \
         -D_POSIX_C_SOURCE=200809L \
         $(shell pkg-config --cflags sdl2)
LDFLAGS = $(shell pkg-config --libs sdl2) -lsqlite3 -lzmq -lm

SRCS = src/main.c \
       src/core/memory.c \
       src/core/hub_context.c \
       src/core/init.c \
       src/core/input_handler.c \
       src/core/process_manager.c \
       src/data/search.c \
       src/data/hub_state.c \
       src/data/layout_manager.c \
       src/net/zmq_transport.c \
       src/ui/font_renderer.c \
       src/ui/draw_primitives.c \
       src/ui/hub_renderer.c \
       src/widgets/widget_system.c \
       src/widgets/now_playing.c \
       src/widgets/mini_visualizer.c \
       src/utils/art_cache.c

BUILD_DIR = build
OBJS = $(SRCS:src/%.c=build/%.o)
TARGET = harmony_hub

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build successful: $(TARGET)"

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
