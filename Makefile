CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -fPIC
LDFLAGS = -pthread

# Includes
INCLUDES = -I./include -I./src \
           -I./deps/luajit/src \
           -I./deps/jemalloc/include

# Libraries
LIBS = deps/luajit/src/libluajit.a \
       deps/jemalloc/lib/libjemalloc.a \
       -lm -ldl

# Source files for the framework
SRCS = src/ae.c \
       src/anet.c \
       src/serverassert.c \
       src/zmalloc.c \
       src/strl.c \
       src/monotonic.c

OBJS = $(SRCS:.c=.o)

# Targets
TARGET_LIB = libfe.a

.PHONY: all clean deps deps-clean examples test

all: deps $(TARGET_LIB) examples test

# Dependency build rules
deps: deps/luajit/src/libluajit.a deps/jemalloc/lib/libjemalloc.a

deps/jemalloc/lib/libjemalloc.a:
	cd deps/jemalloc && ./autogen.sh && $(MAKE)

deps/luajit/src/libluajit.a:
	cd deps/luajit && export MACOSX_DEPLOYMENT_TARGET=11.0 && $(MAKE)

$(TARGET_LIB): $(OBJS)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

examples: $(TARGET_LIB)
	$(CC) $(CFLAGS) $(INCLUDES) examples/example.c $(TARGET_LIB) $(LIBS) -o fsae_example
# 	$(CC) $(CFLAGS) $(INCLUDES) examples/example_lua.c $(TARGET_LIB) $(LIBS) -o example_lua

test: $(TARGET_LIB)
# 	$(CC) $(CFLAGS) $(INCLUDES) test/test_core.c $(TARGET_LIB) $(LIBS) -o test_core
# 	./test_core

clean:
	rm -f $(OBJS) $(TARGET_LIB) fsae_example test_core

deps-clean:
	-cd deps/jemalloc && $(MAKE) clean
	-cd deps/luajit && $(MAKE) clean
