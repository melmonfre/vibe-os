# Build System for Ported GNU Applications
# Compile GNU apps with VibeOS compatibility layer
#
# Usage: make -f Build.ported.mk ported-echo
#        make -f Build.ported.mk ported-cat
#        etc

TOOLCHAIN_PREFIX ?= i686-elf-
HAS_CROSS_TOOLCHAIN := $(shell command -v $(TOOLCHAIN_PREFIX)gcc >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)ld >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)objcopy >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)nm >/dev/null 2>&1 && echo 1 || echo 0)

CC_ORIGIN := $(origin CC)
LD_ORIGIN := $(origin LD)
OBJCOPY_ORIGIN := $(origin OBJCOPY)
NM_ORIGIN := $(origin NM)

ifeq ($(CC_ORIGIN),default)
CC :=
endif
ifeq ($(LD_ORIGIN),default)
LD :=
endif
ifeq ($(OBJCOPY_ORIGIN),default)
OBJCOPY :=
endif
ifeq ($(NM_ORIGIN),default)
NM :=
endif

ifeq ($(strip $(CC)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
CC := $(TOOLCHAIN_PREFIX)gcc
else
CC := gcc
endif
endif

ifeq ($(strip $(LD)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
LD := $(TOOLCHAIN_PREFIX)ld
else
LD := ld
endif
endif

ifeq ($(strip $(OBJCOPY)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
else
OBJCOPY := objcopy
endif
endif

ifeq ($(strip $(NM)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
NM := $(TOOLCHAIN_PREFIX)nm
else
NM := nm
endif
endif

PYTHON ?= python3
CPU_ARCH_CFLAGS := -march=i586 -mtune=generic -mno-mmx -mno-sse -mno-sse2

# Compiler flags - same as other apps
CFLAGS := -m32 $(CPU_ARCH_CFLAGS) -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
	-fno-builtin -nostdlib -Wall -Wextra
INCLUDES := -I. -Icompat/include -Ilang/include -Iapplications/ported/include -Iheaders
LDFLAGS := -m elf_i386 -T linker/app.ld -nostdlib -N --allow-multiple-definition

LIBGCC_A := $(shell $(CC) -m32 $(CPU_ARCH_CFLAGS) -print-libgcc-file-name 2>/dev/null)

# Ported app SDK
APP_ENTRY := lang/sdk/app_entry.c
APP_RUNTIME := lang/sdk/app_runtime.c

# Compat library (built via Build.compat.mk)
COMPAT_LIB := build/libcompat.a
include Build.compat.mk

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
	$(LD) $(LDFLAGS) $(ECHO_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

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
	$(LD) $(LDFLAGS) $(CAT_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

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
	$(LD) $(LDFLAGS) $(WC_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(WC_APP): $(WC_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Wc app: $@"

ported-wc: $(WC_APP)

# === PWD APP ===

PWD_SRCS := applications/ported/pwd/pwd.c
PWD_OBJS := build/ported/pwd.o \
	build/app_entry_pwd.o \
	build/app_runtime_pwd.o

PWD_ELF := build/ported/pwd.elf
PWD_APP := build/ported/pwd.app

build/app_entry_pwd.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"pwd\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_pwd.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/pwd.o: $(PWD_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(PWD_ELF): $(PWD_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(PWD_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(PWD_APP): $(PWD_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Pwd app: $@"

ported-pwd: $(PWD_APP)

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
	$(LD) $(LDFLAGS) $(HEAD_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(HEAD_APP): $(HEAD_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Head app: $@"

ported-head: $(HEAD_APP)

# === SLEEP APP ===

SLEEP_SRCS := applications/ported/sleep/sleep.c
SLEEP_OBJS := build/ported/sleep.o \
	build/app_entry_sleep.o \
	build/app_runtime_sleep.o

SLEEP_ELF := build/ported/sleep.elf
SLEEP_APP := build/ported/sleep.app

build/app_entry_sleep.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"sleep\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_sleep.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/sleep.o: $(SLEEP_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(SLEEP_ELF): $(SLEEP_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(SLEEP_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(SLEEP_APP): $(SLEEP_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Sleep app: $@"

ported-sleep: $(SLEEP_APP)

# === RMDIR APP ===

RMDIR_SRCS := applications/ported/rmdir/rmdir.c
RMDIR_OBJS := build/ported/rmdir.o \
	build/app_entry_rmdir.o \
	build/app_runtime_rmdir.o

RMDIR_ELF := build/ported/rmdir.elf
RMDIR_APP := build/ported/rmdir.app

build/app_entry_rmdir.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"rmdir\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_rmdir.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/rmdir.o: $(RMDIR_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(RMDIR_ELF): $(RMDIR_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(RMDIR_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(RMDIR_APP): $(RMDIR_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Rmdir app: $@"

ported-rmdir: $(RMDIR_APP)

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
	$(LD) $(LDFLAGS) $(TAIL_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

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
	$(LD) $(LDFLAGS) $(GREP_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

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
	$(LD) $(LDFLAGS) $(LOADKEYS_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(LOADKEYS_APP): $(LOADKEYS_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Loadkeys app: $@"

ported-loadkeys: $(LOADKEYS_APP)

# === MKDIR APP ===

MKDIR_SRCS := applications/ported/mkdir/mkdir.c
MKDIR_OBJS := build/ported/mkdir.o \
	build/app_entry_mkdir.o \
	build/app_runtime_mkdir.o

MKDIR_ELF := build/ported/mkdir.elf
MKDIR_APP := build/ported/mkdir.app

build/app_entry_mkdir.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"mkdir\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_mkdir.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/mkdir.o: $(MKDIR_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(MKDIR_ELF): $(MKDIR_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(MKDIR_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(MKDIR_APP): $(MKDIR_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Mkdir app: $@"

ported-mkdir: $(MKDIR_APP)

# === TRUE APP ===

TRUE_SRCS := applications/ported/true/true.c
TRUE_OBJS := build/ported/true.o \
	build/app_entry_true.o \
	build/app_runtime_true.o

TRUE_ELF := build/ported/true.elf
TRUE_APP := build/ported/true.app

build/app_entry_true.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"true\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_true.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/true.o: $(TRUE_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TRUE_ELF): $(TRUE_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(TRUE_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(TRUE_APP): $(TRUE_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ True app: $@"

ported-true: $(TRUE_APP)

# === FALSE APP ===

FALSE_SRCS := applications/ported/false/false.c
FALSE_OBJS := build/ported/false.o \
	build/app_entry_false.o \
	build/app_runtime_false.o

FALSE_ELF := build/ported/false.elf
FALSE_APP := build/ported/false.app

build/app_entry_false.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"false\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_false.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/false.o: $(FALSE_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(FALSE_ELF): $(FALSE_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(FALSE_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(FALSE_APP): $(FALSE_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ False app: $@"

ported-false: $(FALSE_APP)

# === PRINTF APP ===

PRINTF_SRCS := applications/ported/printf/printf.c
PRINTF_OBJS := build/ported/printf.o \
	build/app_entry_printf.o \
	build/app_runtime_printf.o

PRINTF_ELF := build/ported/printf.elf
PRINTF_APP := build/ported/printf.app

build/app_entry_printf.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"printf\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_printf.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/printf.o: $(PRINTF_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(PRINTF_ELF): $(PRINTF_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(PRINTF_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(PRINTF_APP): $(PRINTF_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Printf app: $@"

ported-printf: $(PRINTF_APP)

# === SED APP ===

SED_SRCS := applications/ported/sed/vibe_sed.c
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
	$(CC) $(CFLAGS) -Iapplications/ported/sed $(INCLUDES) -DHAVE_CONFIG_H \
		-Iapplications/ported/sed \
		-Icompat/gnu/lib/libiberty/include \
		-c $< -o $@

$(SED_ELF): $(SED_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(SED_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(SED_APP): $(SED_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Sed app: $@"

ported-sed: $(SED_APP)

# === UNAME APP ===

UNAME_SRCS := applications/ported/uname/uname.c
UNAME_OBJS := build/ported/uname.o \
	build/app_entry_uname.o \
	build/app_runtime_uname.o

UNAME_ELF := build/ported/uname.elf
UNAME_APP := build/ported/uname.app

build/app_entry_uname.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"uname\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_uname.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/uname.o: $(UNAME_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(UNAME_ELF): $(UNAME_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(UNAME_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(UNAME_APP): $(UNAME_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Uname app: $@"

ported-uname: $(UNAME_APP)

# === SYNC APP ===

SYNC_SRCS := applications/ported/sync/sync.c
SYNC_OBJS := build/ported/sync.o \
	build/app_entry_sync.o \
	build/app_runtime_sync.o

SYNC_ELF := build/ported/sync.elf
SYNC_APP := build/ported/sync.app

build/app_entry_sync.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"sync\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_sync.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/sync.o: $(SYNC_SRCS) $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(SYNC_ELF): $(SYNC_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(SYNC_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(SYNC_APP): $(SYNC_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Sync app: $@"

ported-sync: $(SYNC_APP)

# === TR APP ===

TR_SRCS := applications/ported/tr/tr.c applications/ported/tr/str.c
TR_OBJS := $(patsubst applications/ported/tr/%.c,build/ported/tr/%.o,$(TR_SRCS)) \
	build/app_entry_tr.o \
	build/app_runtime_tr.o

TR_ELF := build/ported/tr.elf
TR_APP := build/ported/tr.app

build/app_entry_tr.o: $(APP_ENTRY) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DVIBE_APP_BUILD_NAME=\"tr\" \
		-DVIBE_APP_BUILD_HEAP_SIZE=65536u \
		-c $< -o $@

build/app_runtime_tr.o: $(APP_RUNTIME) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/ported/tr/%.o: applications/ported/tr/%.c applications/ported/tr/extern.h $(COMPAT_LIB) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iapplications/ported/tr $(INCLUDES) -c $< -o $@

$(TR_ELF): $(TR_OBJS) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(TR_OBJS) $(COMPAT_LIB) -o $@ $(LIBGCC_A)

$(TR_APP): $(TR_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $< --bin $@
	@echo "✓ Tr app: $@"

ported-tr: $(TR_APP)

# General rules
build:
	@mkdir -p $@

ported-echo-clean:
	rm -f $(ECHO_OBJS) $(ECHO_ELF) $(ECHO_APP)

ported-cat-clean:
	rm -f $(CAT_OBJS) $(CAT_ELF) $(CAT_APP)

ported-wc-clean:
	rm -f $(WC_OBJS) $(WC_ELF) $(WC_APP)

ported-pwd-clean:
	rm -f $(PWD_OBJS) $(PWD_ELF) $(PWD_APP)

ported-head-clean:
	rm -f $(HEAD_OBJS) $(HEAD_ELF) $(HEAD_APP)

ported-sleep-clean:
	rm -f $(SLEEP_OBJS) $(SLEEP_ELF) $(SLEEP_APP)

ported-rmdir-clean:
	rm -f $(RMDIR_OBJS) $(RMDIR_ELF) $(RMDIR_APP)

ported-tail-clean:
	rm -f $(TAIL_OBJS) $(TAIL_ELF) $(TAIL_APP)

ported-grep-clean:
	rm -f $(GREP_OBJS) $(GREP_ELF) $(GREP_APP)

ported-sed-clean:
	rm -f $(SED_OBJS) $(SED_ELF) $(SED_APP)

ported-loadkeys-clean:
	rm -f $(LOADKEYS_OBJS) $(LOADKEYS_ELF) $(LOADKEYS_APP)

ported-mkdir-clean:
	rm -f $(MKDIR_OBJS) $(MKDIR_ELF) $(MKDIR_APP)

ported-true-clean:
	rm -f $(TRUE_OBJS) $(TRUE_ELF) $(TRUE_APP)

ported-false-clean:
	rm -f $(FALSE_OBJS) $(FALSE_ELF) $(FALSE_APP)

ported-printf-clean:
	rm -f $(PRINTF_OBJS) $(PRINTF_ELF) $(PRINTF_APP)

ported-uname-clean:
	rm -f $(UNAME_OBJS) $(UNAME_ELF) $(UNAME_APP)

ported-sync-clean:
	rm -f $(SYNC_OBJS) $(SYNC_ELF) $(SYNC_APP)

ported-tr-clean:
	rm -f $(TR_OBJS) $(TR_ELF) $(TR_APP)

PORTED_APP_TARGETS := \
	$(ECHO_APP) \
	$(CAT_APP) \
	$(WC_APP) \
	$(PWD_APP) \
	$(HEAD_APP) \
	$(SLEEP_APP) \
	$(RMDIR_APP) \
	$(TAIL_APP) \
	$(GREP_APP) \
	$(SED_APP) \
	$(LOADKEYS_APP) \
	$(MKDIR_APP) \
	$(TRUE_APP) \
	$(FALSE_APP) \
	$(PRINTF_APP) \
	$(UNAME_APP) \
	$(SYNC_APP) \
	$(TR_APP)

ported-all: $(PORTED_APP_TARGETS)

ported-clean: ported-echo-clean ported-cat-clean ported-wc-clean ported-pwd-clean ported-head-clean ported-sleep-clean ported-rmdir-clean ported-tail-clean ported-grep-clean ported-sed-clean ported-loadkeys-clean ported-mkdir-clean ported-true-clean ported-false-clean ported-printf-clean ported-uname-clean ported-sync-clean ported-tr-clean

.PHONY: ported-all ported-echo ported-cat ported-wc ported-pwd ported-head ported-sleep ported-rmdir ported-tail ported-grep ported-sed ported-loadkeys ported-mkdir ported-true ported-false ported-printf ported-uname ported-sync ported-tr ported-clean ported-echo-clean ported-cat-clean ported-wc-clean ported-pwd-clean ported-head-clean ported-sleep-clean ported-rmdir-clean ported-tail-clean ported-grep-clean ported-sed-clean ported-loadkeys-clean ported-mkdir-clean ported-true-clean ported-false-clean ported-printf-clean ported-uname-clean ported-sync-clean ported-tr-clean
