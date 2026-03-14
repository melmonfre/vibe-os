# Build for VibeOS Compatibility Layer
# 
# Compiles libc/POSIX compatibility layer used by ported GNU apps

CC := i686-elf-gcc
AR := i686-elf-ar
RANLIB := i686-elf-ranlib

COMPAT_SRCS := \
	compat/src/libc/string.c \
	compat/src/libc/stdlib.c \
	compat/src/libc/ctype.c \
	compat/src/libc/stdio.c \
	compat/src/libc/assert.c \
	compat/src/posix/errno.c \
	compat/src/posix/unistd.c

COMPAT_OBJS := $(COMPAT_SRCS:%.c=build/%.o)
COMPAT_LIB := build/libcompat.a

# Compilation flags (same as kernel/apps)
COMPAT_CFLAGS := -m32 -Os -ffreestanding -fno-pic -fno-pie \
	-fno-stack-protector -fno-builtin -nostdlib \
	-Wall -Wextra -Werror \
	-I. -Icompat/include -Iheaders -Ilang/include

# Pattern rule for compat objects
build/compat/%.o: compat/%.c | build
	@mkdir -p $(dir $@)
	$(CC) $(COMPAT_CFLAGS) -c $< -o $@

# Archive compat library
$(COMPAT_LIB): $(COMPAT_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(COMPAT_OBJS)
	$(RANLIB) $@
	@echo "✓ compat library: $@"
	@ls -lh $@

compat-build: $(COMPAT_LIB)
	@echo "✓ compat layer built"

compat-clean:
	rm -rf build/compat $(COMPAT_LIB)

.PHONY: compat-build compat-clean
