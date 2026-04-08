TOOLCHAIN_PREFIX ?= i686-elf-
HAS_CROSS_TOOLCHAIN := $(shell command -v $(TOOLCHAIN_PREFIX)gcc >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)ld >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)objcopy >/dev/null 2>&1 && command -v $(TOOLCHAIN_PREFIX)nm >/dev/null 2>&1 && echo 1 || echo 0)

CC_ORIGIN := $(origin CC)
LD_ORIGIN := $(origin LD)
OBJCOPY_ORIGIN := $(origin OBJCOPY)
NM_ORIGIN := $(origin NM)
AR_ORIGIN := $(origin AR)
RANLIB_ORIGIN := $(origin RANLIB)

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
ifeq ($(AR_ORIGIN),default)
AR :=
endif
ifeq ($(RANLIB_ORIGIN),default)
RANLIB :=
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

ifeq ($(strip $(AR)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
AR := $(TOOLCHAIN_PREFIX)ar
else
AR := ar
endif
endif

ifeq ($(strip $(RANLIB)),)
ifeq ($(HAS_CROSS_TOOLCHAIN),1)
RANLIB := $(TOOLCHAIN_PREFIX)ranlib
else
RANLIB := ranlib
endif
endif

PYTHON ?= python3
HOSTCC ?= cc
CPU_ARCH_CFLAGS := -march=i586 -mtune=generic -mno-mmx -mno-sse -mno-sse2
CFLAGS := -m32 $(CPU_ARCH_CFLAGS) -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
	-fno-builtin -nostdlib -Wall -Wextra
INCLUDES := -I. -Iapplications/ported/bsdgames/include -Iapplications/ported/bsdgames -Icompat/include -Ilang/include -Iapplications/ported/include -Iheaders
LDFLAGS := -m elf_i386 -T linker/app.ld -nostdlib -N --allow-multiple-definition

UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
LIBGCC_A := $(shell $(CC) -m32 $(CPU_ARCH_CFLAGS) -print-libgcc-file-name 2>/dev/null)

APP_ENTRY := lang/sdk/app_entry.c
APP_RUNTIME := lang/sdk/app_runtime.c
BSDGAME_WRAPPER := applications/ported/bsdgames/vibe_bsdgame_main.c
BSDGAME_COMPAT := applications/ported/bsdgames/vibe_bsdgame_compat.c
BSDGAME_CURSES := applications/ported/bsdgames/vibe_bsdgame_curses.c
BSDGAME_POSIX := applications/ported/bsdgames/vibe_bsdgame_posix.c
BSDGAME_SHIM := applications/ported/bsdgames/vibe_bsdgame_shim.h

COMPAT_LIB := build/libcompat.a
include Build.compat.mk

BSDGAME_COMMON_OBJ := build/bsdgames/common/vibe_bsdgame_compat.o
BSDGAME_CURSES_OBJ := build/bsdgames/common/vibe_bsdgame_curses.o
BSDGAME_POSIX_OBJ := build/bsdgames/common/vibe_bsdgame_posix.o
BSDGAME_ASSET_TOOL := tools/generate_asset_installer.py
HOST_BSD_COMPAT_C := tools/bsd_host_compat.c
HOST_BSD_COMPAT_H := tools/bsd_host_compat.h
ADVENTURE_DATA_TOOL := build/host-tools/generate_adventure_data
ADVENTURE_DATA_C := build/bsdgames/adventure/data.c
MONOP_INITDECK_TOOL := build/host-tools/monop-initdeck
MONOP_CARDS_PCK := build/bsdgames/monop/cards.pck
MONOP_ASSET_C := build/bsdgames/monop/assets.c
QUIZ_DATFILES := $(sort $(wildcard compat/games/quiz/datfiles/*))
QUIZ_AREAS := build/bsdgames/quiz/areas
QUIZ_ASSET_C := build/bsdgames/quiz/assets.c
BOGGLE_MKDICT_TOOL := build/host-tools/boggle-mkdict
BOGGLE_MKINDEX_TOOL := build/host-tools/boggle-mkindex
BOGGLE_WORDLIST := build/bsdgames/boggle/words.txt
BOGGLE_DICT := build/bsdgames/boggle/dictionary
BOGGLE_DICTINDEX := build/bsdgames/boggle/dictindex
BOGGLE_ASSET_C := build/bsdgames/boggle/assets.c
FORTUNE_DATA_NAMES := fortunes fortunes-o fortunes2 fortunes2-o limerick recipes startrek zippy
FORTUNE_STRFILE_TOOL := build/host-tools/fortune-strfile
FORTUNE_DATFILES := $(patsubst %,build/bsdgames/fortune/%.dat,$(FORTUNE_DATA_NAMES))
FORTUNE_ASSET_C := build/bsdgames/fortune/assets.c
PHANTASIA_ASSET_TOOL := build/host-tools/generate_phantasia_assets
PHANTASIA_MONSTERS := build/bsdgames/phantasia/monsters
PHANTASIA_VOID := build/bsdgames/phantasia/void
PHANTASIA_ASSET_C := build/bsdgames/phantasia/assets.c
ATC_GAME_FILES := $(sort $(wildcard compat/games/atc/games/*))
ATC_ASSET_C := build/bsdgames/atc/assets.c
HACK_MAKEDEFS_TOOL := build/host-tools/hack-makedefs
HACK_ONAMES_H := build/bsdgames/hack/hack.onames.h
HACK_ASSET_C := build/bsdgames/hack/assets.c

$(ADVENTURE_DATA_TOOL): tools/generate_adventure_data.c | build
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 $< -o $@

$(ADVENTURE_DATA_C): compat/games/adventure/glorkz $(ADVENTURE_DATA_TOOL) | build
	@mkdir -p $(dir $@)
	$(ADVENTURE_DATA_TOOL) $< > $@

$(MONOP_INITDECK_TOOL): compat/games/monop/initdeck.c $(HOST_BSD_COMPAT_C) $(HOST_BSD_COMPAT_H) | build
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Icompat/games/monop -include $(HOST_BSD_COMPAT_H) -include arpa/inet.h $< $(HOST_BSD_COMPAT_C) -o $@

$(MONOP_CARDS_PCK): compat/games/monop/cards.inp $(MONOP_INITDECK_TOOL) | build
	@mkdir -p $(dir $@)
	cp $(MONOP_INITDECK_TOOL) $(dir $@)/.monop-initdeck.tmp
	chmod +x $(dir $@)/.monop-initdeck.tmp
	cd $(dir $@) && ./.monop-initdeck.tmp $(abspath compat/games/monop/cards.inp)
	rm -f $(dir $@)/.monop-initdeck.tmp

$(MONOP_ASSET_C): $(MONOP_CARDS_PCK) $(BSDGAME_ASSET_TOOL) | build
	@mkdir -p $(dir $@)
	$(PYTHON) $(BSDGAME_ASSET_TOOL) --output $@ \
		--file /usr/share/games/cards.pck=$(MONOP_CARDS_PCK)

$(QUIZ_AREAS): compat/share/misc/na.phone | build
	@mkdir -p $(dir $@)
	awk 'BEGIN { FS=":"; last="" } { if ($$1 ~ /^[0-9]+$$/) { if (last != $$1) { if ($$4 == "") { print $$1 ":" $$3 ":" $$2; } else { print $$1 ":" $$3 "|" $$4 ":" $$2; } } last = $$1; } }' $< > $@

$(QUIZ_ASSET_C): $(QUIZ_DATFILES) $(QUIZ_AREAS) $(BSDGAME_ASSET_TOOL) | build
	@mkdir -p $(dir $@)
	$(PYTHON) $(BSDGAME_ASSET_TOOL) --output $@ \
		$(foreach file,$(QUIZ_DATFILES),--file /usr/share/games/quiz.db/$(notdir $(file))=$(file)) \
		--file /usr/share/games/quiz.db/areas=$(QUIZ_AREAS)

$(BOGGLE_MKDICT_TOOL): compat/games/boggle/mkdict/mkdict.c $(HOST_BSD_COMPAT_C) $(HOST_BSD_COMPAT_H) | build
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Icompat/games/boggle/boggle -include $(HOST_BSD_COMPAT_H) $< $(HOST_BSD_COMPAT_C) -o $@

$(BOGGLE_MKINDEX_TOOL): compat/games/boggle/mkindex/mkindex.c $(HOST_BSD_COMPAT_C) $(HOST_BSD_COMPAT_H) | build
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Icompat/games/boggle/boggle -include $(HOST_BSD_COMPAT_H) $< $(HOST_BSD_COMPAT_C) -o $@

$(BOGGLE_WORDLIST): compat/share/dict/web2 compat/share/dict/web2a | build
	@mkdir -p $(dir $@)
	cat $^ | LC_ALL=C sort -u > $@

$(BOGGLE_DICT): $(BOGGLE_WORDLIST) $(BOGGLE_MKDICT_TOOL) | build
	@mkdir -p $(dir $@)
	$(BOGGLE_MKDICT_TOOL) < $< > $@

$(BOGGLE_DICTINDEX): $(BOGGLE_DICT) $(BOGGLE_MKINDEX_TOOL) | build
	@mkdir -p $(dir $@)
	$(BOGGLE_MKINDEX_TOOL) < $< > $@

$(BOGGLE_ASSET_C): compat/games/boggle/boggle/helpfile $(BOGGLE_DICT) $(BOGGLE_DICTINDEX) $(BSDGAME_ASSET_TOOL) | build
	@mkdir -p $(dir $@)
	$(PYTHON) $(BSDGAME_ASSET_TOOL) --output $@ \
		--file /usr/share/games/boggle/helpfile=compat/games/boggle/boggle/helpfile \
		--file /usr/share/games/boggle/dictionary=$(BOGGLE_DICT) \
		--file /usr/share/games/boggle/dictindex=$(BOGGLE_DICTINDEX)

$(FORTUNE_STRFILE_TOOL): compat/games/fortune/strfile/strfile.c $(HOST_BSD_COMPAT_C) $(HOST_BSD_COMPAT_H) | build
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Icompat/games/fortune/strfile -include $(HOST_BSD_COMPAT_H) -include arpa/inet.h $< $(HOST_BSD_COMPAT_C) -o $@

$(FORTUNE_ASSET_C): $(foreach name,$(FORTUNE_DATA_NAMES),compat/games/fortune/datfiles/$(name)) $(FORTUNE_STRFILE_TOOL) $(BSDGAME_ASSET_TOOL) | build
	@mkdir -p $(dir $@)
	$(FORTUNE_STRFILE_TOOL) -s compat/games/fortune/datfiles/fortunes build/bsdgames/fortune/fortunes.dat
	$(FORTUNE_STRFILE_TOOL) -s compat/games/fortune/datfiles/fortunes-o build/bsdgames/fortune/fortunes-o.dat
	$(FORTUNE_STRFILE_TOOL) -s compat/games/fortune/datfiles/fortunes2 build/bsdgames/fortune/fortunes2.dat
	$(FORTUNE_STRFILE_TOOL) -s compat/games/fortune/datfiles/fortunes2-o build/bsdgames/fortune/fortunes2-o.dat
	$(FORTUNE_STRFILE_TOOL) -s compat/games/fortune/datfiles/limerick build/bsdgames/fortune/limerick.dat
	$(FORTUNE_STRFILE_TOOL) -s compat/games/fortune/datfiles/recipes build/bsdgames/fortune/recipes.dat
	$(FORTUNE_STRFILE_TOOL) -s compat/games/fortune/datfiles/startrek build/bsdgames/fortune/startrek.dat
	$(FORTUNE_STRFILE_TOOL) -s compat/games/fortune/datfiles/zippy build/bsdgames/fortune/zippy.dat
	$(PYTHON) $(BSDGAME_ASSET_TOOL) --output $@ \
		$(foreach name,$(FORTUNE_DATA_NAMES),--file /usr/share/games/fortune/$(name)=compat/games/fortune/datfiles/$(name) --file /usr/share/games/fortune/$(name).dat=build/bsdgames/fortune/$(name).dat)

$(PHANTASIA_ASSET_TOOL): tools/generate_phantasia_assets.c | build
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -I. $< -o $@

$(PHANTASIA_MONSTERS) $(PHANTASIA_VOID): compat/games/phantasia/monsters.asc $(PHANTASIA_ASSET_TOOL) | build
	@mkdir -p $(dir $(PHANTASIA_MONSTERS))
	$(PHANTASIA_ASSET_TOOL) $< $(PHANTASIA_MONSTERS) $(PHANTASIA_VOID)

$(PHANTASIA_ASSET_C): $(PHANTASIA_MONSTERS) $(PHANTASIA_VOID) $(BSDGAME_ASSET_TOOL) | build
	@mkdir -p $(dir $@)
	$(PYTHON) $(BSDGAME_ASSET_TOOL) --output $@ \
		--file /var/games/phantasia/monsters=$(PHANTASIA_MONSTERS) \
		--file /var/games/phantasia/void=$(PHANTASIA_VOID) \
		--empty /var/games/phantasia/characs \
		--empty /var/games/phantasia/gold \
		--empty /var/games/phantasia/lastdead \
		--empty /var/games/phantasia/mess \
		--empty /var/games/phantasia/motd \
		--empty /var/games/phantasia/scoreboard

$(ATC_ASSET_C): $(ATC_GAME_FILES) $(BSDGAME_ASSET_TOOL) | build
	@mkdir -p $(dir $@)
	$(PYTHON) $(BSDGAME_ASSET_TOOL) --output $@ \
		$(foreach file,$(ATC_GAME_FILES),--file /usr/share/games/atc/$(notdir $(file))=$(file))

$(HACK_MAKEDEFS_TOOL): compat/games/hack/makedefs.c | build
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -Icompat/games/hack -include $(HOST_BSD_COMPAT_H) $< $(HOST_BSD_COMPAT_C) -o $@

$(HACK_ONAMES_H): compat/games/hack/def.objects.h $(HACK_MAKEDEFS_TOOL) | build
	@mkdir -p $(dir $@)
	$(HACK_MAKEDEFS_TOOL) $< > $@

$(HACK_ASSET_C): compat/games/hack/help compat/games/hack/hh compat/games/hack/data compat/games/hack/rumors $(BSDGAME_ASSET_TOOL) | build
	@mkdir -p $(dir $@)
	$(PYTHON) $(BSDGAME_ASSET_TOOL) --output $@ \
		--file /usr/share/games/hack/help=compat/games/hack/help \
		--file /usr/share/games/hack/hh=compat/games/hack/hh \
		--file /usr/share/games/hack/data=compat/games/hack/data \
		--file /usr/share/games/hack/rumors=compat/games/hack/rumors \
		--empty /record \
		--empty /perm

build/bsdgames/common/vibe_bsdgame_compat.o: $(BSDGAME_COMPAT) $(BSDGAME_SHIM) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/bsdgames/common/vibe_bsdgame_curses.o: $(BSDGAME_CURSES) $(BSDGAME_SHIM) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/bsdgames/common/vibe_bsdgame_posix.o: $(BSDGAME_POSIX) $(BSDGAME_SHIM) | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

define BUILD_BSDGAME
$(1)_UPSTREAM_SRCS := $(2)
$(1)_GAME_OBJS := $$(patsubst %.c,build/bsdgames/$(1)/%.o,$$($(1)_UPSTREAM_SRCS))
$(1)_APP_ENTRY_OBJ := build/bsdgames/$(1)/app_entry.o
$(1)_APP_RUNTIME_OBJ := build/bsdgames/$(1)/app_runtime.o
$(1)_WRAPPER_OBJ := build/bsdgames/$(1)/vibe_bsdgame_main.o
$(1)_OBJS := $$($(1)_GAME_OBJS) $$($(1)_WRAPPER_OBJ) $$($(1)_APP_ENTRY_OBJ) $$($(1)_APP_RUNTIME_OBJ)
$(1)_ELF := build/ported/$(1).elf
$(1)_APP := build/ported/$(1).app

build/bsdgames/$(1)/%.o: %.c $(BSDGAME_SHIM) $(BSDGAME_COMPAT) $(COMPAT_LIB) | build
	@mkdir -p $$(dir $$@)
	$(CC) $(CFLAGS) $(INCLUDES) $(3) -Dmain=vibe_bsdgame_main -include $(BSDGAME_SHIM) -c $$< -o $$@

$$($(1)_WRAPPER_OBJ): $(BSDGAME_WRAPPER) $(BSDGAME_SHIM) | build
	@mkdir -p $$(dir $$@)
	$(CC) $(CFLAGS) $(INCLUDES) $(3) -DVIBE_BSDGAME_FALLBACK_NAME=\"$(1)\" -c $$< -o $$@

$$($(1)_APP_ENTRY_OBJ): $(APP_ENTRY) | build
	@mkdir -p $$(dir $$@)
	$(CC) $(CFLAGS) $(INCLUDES) -DVIBE_APP_BUILD_NAME=\"$(1)\" -DVIBE_APP_BUILD_HEAP_SIZE=$(4) -c $$< -o $$@

$$($(1)_APP_RUNTIME_OBJ): $(APP_RUNTIME) | build
	@mkdir -p $$(dir $$@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $$< -o $$@

$$($(1)_ELF): $$($(1)_OBJS) $(BSDGAME_COMMON_OBJ) $(BSDGAME_CURSES_OBJ) $(BSDGAME_POSIX_OBJ) $(COMPAT_LIB) linker/app.ld | build
	@mkdir -p $$(dir $$@)
	$(LD) $(LDFLAGS) $$($(1)_OBJS) $(BSDGAME_COMMON_OBJ) $(BSDGAME_CURSES_OBJ) $(BSDGAME_POSIX_OBJ) $(COMPAT_LIB) -o $$@ $(LIBGCC_A)

$$($(1)_APP): $$($(1)_ELF)
	@mkdir -p $$(dir $$@)
	$(OBJCOPY) -O binary $$< $$@
	$(PYTHON) tools/patch_app_header.py --nm $(NM) --elf $$< --bin $$@
	@echo "✓ BSD game app: $$@"

bsdgame-$(1): $$($(1)_APP)
endef

$(eval $(call BUILD_BSDGAME,adventure,compat/games/adventure/crc.c compat/games/adventure/done.c $(ADVENTURE_DATA_C) compat/games/adventure/init.c compat/games/adventure/io.c compat/games/adventure/main.c compat/games/adventure/save.c compat/games/adventure/subr.c compat/games/adventure/vocab.c compat/games/adventure/wizard.c,-Icompat/games/adventure,262144u))
$(eval $(call BUILD_BSDGAME,arithmetic,compat/games/arithmetic/arithmetic.c,-Icompat/games/arithmetic,131072u))
$(eval $(call BUILD_BSDGAME,atc,compat/games/atc/extern.c applications/ported/bsdgames/atc_parser_manual.c compat/games/atc/graphics.c compat/games/atc/input.c compat/games/atc/list.c compat/games/atc/log.c compat/games/atc/main.c compat/games/atc/update.c $(ATC_ASSET_C),-Icompat/games/atc -Iapplications/ported/bsdgames,524288u))
$(eval $(call BUILD_BSDGAME,banner,compat/games/banner/banner.c,-Icompat/games/banner,65536u))
$(eval $(call BUILD_BSDGAME,bcd,compat/games/bcd/bcd.c,-Icompat/games/bcd,65536u))
$(eval $(call BUILD_BSDGAME,battlestar,compat/games/battlestar/battlestar.c compat/games/battlestar/command1.c compat/games/battlestar/command2.c compat/games/battlestar/command3.c compat/games/battlestar/command4.c compat/games/battlestar/command5.c compat/games/battlestar/command6.c compat/games/battlestar/command7.c compat/games/battlestar/init.c compat/games/battlestar/cypher.c compat/games/battlestar/getcom.c compat/games/battlestar/parse.c compat/games/battlestar/room.c compat/games/battlestar/save.c compat/games/battlestar/fly.c compat/games/battlestar/misc.c compat/games/battlestar/globals.c compat/games/battlestar/dayfile.c compat/games/battlestar/nightfile.c compat/games/battlestar/dayobjs.c compat/games/battlestar/nightobjs.c compat/games/battlestar/words.c,-Icompat/games/battlestar,262144u))
$(eval $(call BUILD_BSDGAME,caesar,compat/games/caesar/caesar.c,-Icompat/games/caesar,131072u))
$(eval $(call BUILD_BSDGAME,canfield,compat/games/canfield/canfield/canfield.c,-Icompat/games/canfield/canfield,262144u))
$(eval $(call BUILD_BSDGAME,cribbage,compat/games/cribbage/extern.c compat/games/cribbage/crib.c compat/games/cribbage/cards.c compat/games/cribbage/instr.c compat/games/cribbage/io.c compat/games/cribbage/score.c compat/games/cribbage/support.c,-Icompat/games/cribbage,262144u))
$(eval $(call BUILD_BSDGAME,factor,compat/games/factor/factor.c compat/games/primes/pattern.c compat/games/primes/pr_tbl.c,-Icompat/games/factor -Icompat/games/primes,131072u))
$(eval $(call BUILD_BSDGAME,fish,compat/games/fish/fish.c,-Icompat/games/fish,262144u))
$(eval $(call BUILD_BSDGAME,bs,compat/games/bs/bs.c,-Icompat/games/bs,262144u))
$(eval $(call BUILD_BSDGAME,gomoku,compat/games/gomoku/bdinit.c compat/games/gomoku/bdisp.c compat/games/gomoku/main.c compat/games/gomoku/makemove.c compat/games/gomoku/pickmove.c compat/games/gomoku/stoc.c,-Icompat/games/gomoku,262144u))
$(eval $(call BUILD_BSDGAME,grdc,compat/games/grdc/grdc.c,-Icompat/games/grdc,262144u))
$(eval $(call BUILD_BSDGAME,hack,compat/games/hack/alloc.c compat/games/hack/hack.Decl.c compat/games/hack/hack.apply.c compat/games/hack/hack.bones.c compat/games/hack/hack.c compat/games/hack/hack.cmd.c compat/games/hack/hack.do.c compat/games/hack/hack.do_name.c compat/games/hack/hack.do_wear.c compat/games/hack/hack.dog.c compat/games/hack/hack.eat.c compat/games/hack/hack.end.c compat/games/hack/hack.engrave.c compat/games/hack/hack.fight.c compat/games/hack/hack.invent.c compat/games/hack/hack.ioctl.c compat/games/hack/hack.lev.c compat/games/hack/hack.main.c compat/games/hack/hack.makemon.c compat/games/hack/hack.mhitu.c compat/games/hack/hack.mklev.c compat/games/hack/hack.mkmaze.c compat/games/hack/hack.mkobj.c compat/games/hack/hack.mkshop.c compat/games/hack/hack.mon.c compat/games/hack/hack.monst.c compat/games/hack/hack.o_init.c compat/games/hack/hack.objnam.c compat/games/hack/hack.options.c compat/games/hack/hack.pager.c compat/games/hack/hack.potion.c compat/games/hack/hack.pri.c compat/games/hack/hack.read.c compat/games/hack/hack.rip.c compat/games/hack/hack.rumors.c compat/games/hack/hack.save.c compat/games/hack/hack.search.c compat/games/hack/hack.shk.c compat/games/hack/hack.shknam.c compat/games/hack/hack.steal.c applications/ported/bsdgames/hack_screen_vibe.c compat/games/hack/hack.timeout.c compat/games/hack/hack.topl.c compat/games/hack/hack.track.c compat/games/hack/hack.trap.c compat/games/hack/hack.tty.c compat/games/hack/hack.u_init.c compat/games/hack/hack.unix.c compat/games/hack/hack.vault.c compat/games/hack/hack.version.c compat/games/hack/hack.wield.c compat/games/hack/hack.wizard.c compat/games/hack/hack.worm.c compat/games/hack/hack.worn.c compat/games/hack/hack.zap.c compat/games/hack/rnd.c $(HACK_ASSET_C),-Icompat/games/hack -Ibuild/bsdgames/hack -Dbsdgame_printf=hack_screen_printf -Dbsdgame_vprintf=hack_screen_vprintf -Dputchar=hack_screen_putchar -Dputs=hack_screen_puts -Dfputs=hack_screen_fputs,2097152u))
$(eval $(call BUILD_BSDGAME,hangman,compat/games/hangman/endgame.c compat/games/hangman/extern.c compat/games/hangman/getguess.c compat/games/hangman/getword.c applications/ported/bsdgames/hangman_ksyms_fallback.c compat/games/hangman/main.c compat/games/hangman/playgame.c compat/games/hangman/prdata.c compat/games/hangman/prman.c compat/games/hangman/prword.c compat/games/hangman/setup.c,-Icompat/games/hangman,262144u))
$(eval $(call BUILD_BSDGAME,backgammon,compat/games/backgammon/common_source/allow.c compat/games/backgammon/common_source/board.c compat/games/backgammon/common_source/check.c compat/games/backgammon/backgammon/extra.c compat/games/backgammon/common_source/fancy.c compat/games/backgammon/common_source/init.c compat/games/backgammon/backgammon/main.c compat/games/backgammon/backgammon/move.c compat/games/backgammon/common_source/odds.c compat/games/backgammon/backgammon/pubeval.c compat/games/backgammon/common_source/one.c compat/games/backgammon/common_source/save.c compat/games/backgammon/common_source/subs.c compat/games/backgammon/common_source/table.c compat/games/backgammon/backgammon/text.c,-Icompat/games/backgammon/common_source -Icompat/games/backgammon/backgammon,262144u))
$(eval $(call BUILD_BSDGAME,boggle,compat/games/boggle/boggle/bog.c compat/games/boggle/boggle/help.c compat/games/boggle/boggle/mach.c compat/games/boggle/boggle/prtable.c compat/games/boggle/boggle/timer.c compat/games/boggle/boggle/word.c $(BOGGLE_ASSET_C),-Icompat/games/boggle/boggle,6291456u))
$(eval $(call BUILD_BSDGAME,teachgammon,compat/games/backgammon/common_source/allow.c compat/games/backgammon/common_source/board.c compat/games/backgammon/common_source/check.c compat/games/backgammon/teachgammon/data.c compat/games/backgammon/common_source/fancy.c compat/games/backgammon/common_source/init.c compat/games/backgammon/common_source/odds.c compat/games/backgammon/common_source/one.c compat/games/backgammon/common_source/save.c compat/games/backgammon/common_source/subs.c compat/games/backgammon/common_source/table.c compat/games/backgammon/teachgammon/teach.c compat/games/backgammon/teachgammon/ttext1.c compat/games/backgammon/teachgammon/ttext2.c compat/games/backgammon/teachgammon/tutor.c,-Icompat/games/backgammon/common_source -Icompat/games/backgammon/teachgammon,262144u))
$(eval $(call BUILD_BSDGAME,fortune,compat/games/fortune/fortune/fortune.c $(FORTUNE_ASSET_C),-Icompat/games/fortune/fortune -Icompat/games/fortune/strfile,4194304u))
$(eval $(call BUILD_BSDGAME,mille,compat/games/mille/comp.c compat/games/mille/end.c compat/games/mille/extern.c compat/games/mille/init.c compat/games/mille/mille.c compat/games/mille/misc.c compat/games/mille/move.c compat/games/mille/print.c compat/games/mille/roll.c compat/games/mille/save.c compat/games/mille/types.c compat/games/mille/varpush.c,-Icompat/games/mille,262144u))
$(eval $(call BUILD_BSDGAME,monop,compat/games/monop/monop.c compat/games/monop/cards.c compat/games/monop/execute.c compat/games/monop/getinp.c compat/games/monop/houses.c compat/games/monop/jail.c compat/games/monop/misc.c compat/games/monop/morg.c compat/games/monop/print.c compat/games/monop/prop.c compat/games/monop/rent.c compat/games/monop/roll.c compat/games/monop/spec.c compat/games/monop/trade.c $(MONOP_ASSET_C),-Icompat/games/monop,524288u))
$(eval $(call BUILD_BSDGAME,morse,compat/games/morse/morse.c,-Icompat/games/morse,65536u))
$(eval $(call BUILD_BSDGAME,number,compat/games/number/number.c,-Icompat/games/number,131072u))
$(eval $(call BUILD_BSDGAME,phantasia,compat/games/phantasia/main.c compat/games/phantasia/fight.c compat/games/phantasia/gamesupport.c compat/games/phantasia/interplayer.c compat/games/phantasia/io.c compat/games/phantasia/misc.c compat/games/phantasia/phantglobs.c $(PHANTASIA_ASSET_C),-Icompat/games/phantasia -DTERMIOS,1048576u))
$(eval $(call BUILD_BSDGAME,pig,compat/games/pig/pig.c,-Icompat/games/pig,65536u))
$(eval $(call BUILD_BSDGAME,pom,compat/games/pom/pom.c,-Icompat/games/pom,131072u))
$(eval $(call BUILD_BSDGAME,ppt,compat/games/ppt/ppt.c,-Icompat/games/ppt,65536u))
$(eval $(call BUILD_BSDGAME,primes,compat/games/primes/primes.c compat/games/primes/pattern.c compat/games/primes/pr_tbl.c,-Icompat/games/primes,131072u))
$(eval $(call BUILD_BSDGAME,quiz,compat/games/quiz/quiz.c compat/games/quiz/rxp.c $(QUIZ_ASSET_C),-Icompat/games/quiz,524288u))
$(eval $(call BUILD_BSDGAME,random,compat/games/random/random.c,-Icompat/games/random,131072u))
$(eval $(call BUILD_BSDGAME,rain,compat/games/rain/rain.c,-Icompat/games/rain,131072u))
$(eval $(call BUILD_BSDGAME,robots,compat/games/robots/extern.c compat/games/robots/init_field.c compat/games/robots/main.c compat/games/robots/make_level.c compat/games/robots/move.c compat/games/robots/move_robs.c compat/games/robots/play_level.c compat/games/robots/query.c compat/games/robots/rnd_pos.c compat/games/robots/score.c,-Icompat/games/robots,262144u))
$(eval $(call BUILD_BSDGAME,sail,compat/games/sail/main.c compat/games/sail/pl_main.c compat/games/sail/pl_1.c compat/games/sail/pl_2.c compat/games/sail/pl_3.c compat/games/sail/pl_4.c compat/games/sail/pl_5.c compat/games/sail/pl_6.c compat/games/sail/pl_7.c compat/games/sail/dr_main.c compat/games/sail/dr_1.c compat/games/sail/dr_2.c compat/games/sail/dr_3.c compat/games/sail/dr_4.c compat/games/sail/dr_5.c compat/games/sail/lo_main.c compat/games/sail/assorted.c compat/games/sail/game.c compat/games/sail/globals.c compat/games/sail/misc.c compat/games/sail/parties.c compat/games/sail/sync.c compat/games/sail/version.c applications/ported/bsdgames/sail_single_process.c,-Icompat/games/sail -Iapplications/ported/bsdgames,786432u))
$(eval $(call BUILD_BSDGAME,snake-bsd,compat/games/snake/snake.c,-Icompat/games/snake,262144u))
$(eval $(call BUILD_BSDGAME,tetris-bsd,compat/games/tetris/input.c applications/ported/bsdgames/tetris_screen_vibe.c compat/games/tetris/shapes.c compat/games/tetris/scores.c compat/games/tetris/tetris.c,-Icompat/games/tetris,262144u))
$(eval $(call BUILD_BSDGAME,trek,compat/games/trek/abandon.c compat/games/trek/attack.c compat/games/trek/autover.c compat/games/trek/capture.c compat/games/trek/check_out.c compat/games/trek/checkcond.c compat/games/trek/compkl.c compat/games/trek/computer.c compat/games/trek/damage.c compat/games/trek/damaged.c compat/games/trek/dcrept.c compat/games/trek/destruct.c compat/games/trek/dock.c compat/games/trek/dumpme.c compat/games/trek/dumpssradio.c compat/games/trek/events.c compat/games/trek/externs.c compat/games/trek/getcodi.c compat/games/trek/getpar.c compat/games/trek/help.c compat/games/trek/impulse.c compat/games/trek/initquad.c compat/games/trek/kill.c compat/games/trek/klmove.c compat/games/trek/lose.c compat/games/trek/lrscan.c compat/games/trek/main.c compat/games/trek/move.c compat/games/trek/nova.c compat/games/trek/out.c compat/games/trek/phaser.c compat/games/trek/play.c compat/games/trek/ram.c compat/games/trek/ranf.c compat/games/trek/rest.c compat/games/trek/schedule.c compat/games/trek/score.c compat/games/trek/setup.c compat/games/trek/setwarp.c compat/games/trek/shield.c compat/games/trek/snova.c compat/games/trek/srscan.c compat/games/trek/systemname.c compat/games/trek/torped.c compat/games/trek/visual.c compat/games/trek/warp.c compat/games/trek/win.c,-Icompat/games/trek,262144u))
$(eval $(call BUILD_BSDGAME,wargames,applications/ported/bsdgames/wargames_vibe.c,-Iapplications/ported/bsdgames,65536u))
$(eval $(call BUILD_BSDGAME,worm,compat/games/worm/worm.c,-Icompat/games/worm,262144u))
$(eval $(call BUILD_BSDGAME,worms,compat/games/worms/worms.c,-Icompat/games/worms,262144u))
$(eval $(call BUILD_BSDGAME,wump,compat/games/wump/wump.c,-Icompat/games/wump,262144u))

$(hack_GAME_OBJS): $(HACK_ONAMES_H)

BSDGAME_APPS := \
	$(adventure_APP) \
	$(arithmetic_APP) \
	$(atc_APP) \
	$(backgammon_APP) \
	$(banner_APP) \
	$(bcd_APP) \
	$(battlestar_APP) \
	$(boggle_APP) \
	$(bs_APP) \
	$(caesar_APP) \
	$(canfield_APP) \
	$(cribbage_APP) \
	$(factor_APP) \
	$(fish_APP) \
	$(fortune_APP) \
	$(gomoku_APP) \
	$(grdc_APP) \
	$(hack_APP) \
	$(hangman_APP) \
	$(mille_APP) \
	$(monop_APP) \
	$(morse_APP) \
	$(number_APP) \
	$(phantasia_APP) \
	$(pig_APP) \
	$(pom_APP) \
	$(ppt_APP) \
	$(primes_APP) \
	$(quiz_APP) \
	$(rain_APP) \
	$(random_APP) \
	$(robots_APP) \
	$(sail_APP) \
	$(snake-bsd_APP) \
	$(tetris-bsd_APP) \
	$(teachgammon_APP) \
	$(trek_APP) \
	$(wargames_APP) \
	$(worm_APP) \
	$(worms_APP) \
	$(wump_APP)

bsdgames-all: $(BSDGAME_APPS)

.PHONY: bsdgames-all \
	bsdgame-adventure bsdgame-arithmetic bsdgame-atc bsdgame-backgammon bsdgame-banner bsdgame-battlestar bsdgame-bcd bsdgame-boggle bsdgame-caesar bsdgame-canfield \
	bsdgame-bs bsdgame-cribbage bsdgame-factor bsdgame-fish bsdgame-fortune bsdgame-gomoku bsdgame-grdc bsdgame-hack bsdgame-hangman bsdgame-mille bsdgame-monop bsdgame-morse bsdgame-number \
	bsdgame-phantasia bsdgame-pig bsdgame-pom bsdgame-ppt bsdgame-primes bsdgame-quiz bsdgame-rain \
	bsdgame-random bsdgame-robots bsdgame-sail bsdgame-snake-bsd bsdgame-teachgammon bsdgame-tetris-bsd bsdgame-trek bsdgame-wargames \
	bsdgame-worm bsdgame-worms bsdgame-wump
