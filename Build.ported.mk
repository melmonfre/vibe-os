# Build System for Ported GNU Applications
# Compile GNU apps with VibeOS compatibility layer
#
# Usage: make -f Build.ported.mk ported-echo
#        make -f Build.ported.mk ported-cat
#        etc

CC := i686-elf-gcc
LD := i686-elf-ld
OBJCOPY := i686-elf-objcopy
NM := i686-elf-nm
PYTHON := python3

# Compiler flags - same as other apps
CFLAGS := -m32 -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
	-fno-builtin -nostdlib -Wall -Wextra -Werror
INCLUDES := -I. -Icompat/include -Iheaders -Ilang/include -Iapplications/ported/include
LDFLAGS := -m elf_i386 -T linker/app.ld -nostdlib -N --allow-multiple-definition

# Ported app SDK
APP_ENTRY := lang/sdk/app_entry.c
APP_RUNTIME := lang/sdk/app_runtime.c

# Compat library (built via Build.compat.mk)
COMPAT_LIB := build/libcompat.a

# === ECHO APP ===

ECHO_SRCS := applications/ported/echo/echo.c
ECHO_OBJS := build/ported/echo.o \
	build/app_entry_echo.o \
	build/app_runtime_echo.o

ECHO_ELF := build/ported/echo.elf
ECHO_APP := build/ported/echo.app

build/app_entry_echo.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"echo\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_echo.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/echo.o: $(ECHO_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(ECHO_ELF): $(ECHO_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(ECHO_OBJS) $(COMPAT_LIB) -o $@

$(ECHO_APP): $(ECHO_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Echo app: $@"

ported-echo: $(ECHO_APP)

# === CAT APP ===

CAT_SRCS := applications/ported/cat/cat.c
CAT_OBJS := build/ported/cat.o \
	build/app_entry_cat.o \
	build/app_runtime_cat.o

CAT_ELF := build/ported/cat.elf
CAT_APP := build/ported/cat.app

build/app_entry_cat.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"cat\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_cat.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/cat.o: $(CAT_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(CAT_ELF): $(CAT_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(CAT_OBJS) $(COMPAT_LIB) -o $@

$(CAT_APP): $(CAT_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Cat app: $@"

ported-cat: $(CAT_APP)

# === WC APP ===

WC_SRCS := applications/ported/wc/wc.c
WC_OBJS := build/ported/wc.o \
	build/app_entry_wc.o \
	build/app_runtime_wc.o

WC_ELF := build/ported/wc.elf
WC_APP := build/ported/wc.app

build/app_entry_wc.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"wc\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_wc.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/wc.o: $(WC_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(WC_ELF): $(WC_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(WC_OBJS) $(COMPAT_LIB) -o $@

$(WC_APP): $(WC_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Wc app: $@"

ported-wc: $(WC_APP)

# === HEAD APP ===

HEAD_SRCS := applications/ported/head/head.c
HEAD_OBJS := build/ported/head.o \
	build/app_entry_head.o \
	build/app_runtime_head.o

HEAD_ELF := build/ported/head.elf
HEAD_APP := build/ported/head.app

build/app_entry_head.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"head\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_head.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/head.o: $(HEAD_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(HEAD_ELF): $(HEAD_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(HEAD_OBJS) $(COMPAT_LIB) -o $@

$(HEAD_APP): $(HEAD_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Head app: $@"

ported-head: $(HEAD_APP)

# === TAIL APP ===

TAIL_SRCS := applications/ported/tail/tail.c
TAIL_OBJS := build/ported/tail.o \
	build/app_entry_tail.o \
	build/app_runtime_tail.o

TAIL_ELF := build/ported/tail.elf
TAIL_APP := build/ported/tail.app

build/app_entry_tail.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"tail\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_tail.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/tail.o: $(TAIL_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TAIL_ELF): $(TAIL_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(TAIL_OBJS) $(COMPAT_LIB) -o $@

$(TAIL_APP): $(TAIL_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Tail app: $@"

ported-tail: $(TAIL_APP)

# === GREP APP ===

GREP_SRCS := applications/ported/grep/grep.c
GREP_OBJS := build/ported/grep.o \
	build/app_entry_grep.o \
	build/app_runtime_grep.o

GREP_ELF := build/ported/grep.elf
GREP_APP := build/ported/grep.app

build/app_entry_grep.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"grep\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_grep.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/grep.o: $(GREP_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(GREP_ELF): $(GREP_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(GREP_OBJS) $(COMPAT_LIB) -o $@

$(GREP_APP): $(GREP_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Grep app: $@"

ported-grep: $(GREP_APP)

# === LOADKEYS APP ===

LOADKEYS_SRCS := applications/ported/loadkeys/loadkeys.c
LOADKEYS_OBJS := build/ported/loadkeys.o \
	build/app_entry_loadkeys.o \
	build/app_runtime_loadkeys.o

LOADKEYS_ELF := build/ported/loadkeys.elf
LOADKEYS_APP := build/ported/loadkeys.app

build/app_entry_loadkeys.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"loadkeys\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_loadkeys.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/loadkeys.o: $(LOADKEYS_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(LOADKEYS_ELF): $(LOADKEYS_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(LOADKEYS_OBJS) $(COMPAT_LIB) -o $@

$(LOADKEYS_APP): $(LOADKEYS_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Loadkeys app: $@"

ported-loadkeys: $(LOADKEYS_APP)

# === SED APP ===

SED_SRCS := $(wildcard applications/ported/sed/*.c)
SED_OBJS := $(patsubst applications/ported/sed/%.c,build/ported/sed/%.o,$(SED_SRCS)) \
	build/app_entry_sed.o \
	build/app_runtime_sed.o

SED_ELF := build/ported/sed.elf
SED_APP := build/ported/sed.app

build/app_entry_sed.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"sed\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=262144u \
		-c $< -o $@

build/app_runtime_sed.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/sed/%.o: applications/ported/sed/%.c $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(SED_ELF): $(SED_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(SED_OBJS) $(COMPAT_LIB) -o $@

$(SED_APP): $(SED_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Sed app: $@"

ported-sed: $(SED_APP)

# General rules
build:
	@mkdir -p $@

ported-echo-clean:
	rm -f $(ECHO_OBJS) $(ECHO_ELF) $(ECHO_APP)

ported-cat-clean:
	rm -f $(CAT_OBJS) $(CAT_ELF) $(CAT_APP)

ported-wc-clean:
	rm -f $(WC_OBJS) $(WC_ELF) $(WC_APP)

ported-head-clean:
	rm -f $(HEAD_OBJS) $(HEAD_ELF) $(HEAD_APP)

ported-tail-clean:
	rm -f $(TAIL_OBJS) $(TAIL_ELF) $(TAIL_APP)

ported-grep-clean:
	rm -f $(GREP_OBJS) $(GREP_ELF) $(GREP_APP)

ported-sed-clean:
	rm -f $(SED_OBJS) $(SED_ELF) $(SED_APP)

ported-loadkeys-clean:
	rm -f $(LOADKEYS_OBJS) $(LOADKEYS_ELF) $(LOADKEYS_APP)

ported-clean: ported-echo-clean ported-cat-clean ported-wc-clean ported-head-clean ported-tail-clean ported-grep-clean ported-sed-clean ported-loadkeys-clean

.PHONY: ported-echo ported-cat ported-wc ported-head ported-tail ported-grep ported-sed ported-loadkeys ported-clean ported-echo-clean ported-cat-clean ported-wc-clean ported-head-clean ported-tail-clean ported-grep-clean ported-sed-clean ported-loadkeys-clean
