# VibeOS Compatibility & GNU Porting Plan

## Date: March 13, 2026

### CURRENT STATE AUDIT

#### Existing FAKE/STUB Commands (in busybox.c)
```
cmd_echo()     - 7 lines: just loops args and prints
cmd_cat()      - 10 lines: reads from VFS, prints with data ptr directly
cmd_clear()    - exists
cmd_uname()    - exists  
cmd_help()     - exists
pwd, ls, cd, mkdir, touch, rm, exit - filesystem navigation
```

#### Existing App Infrastructure
- Apps are .app binaries in AppFS (LBA 385-392 directory, 393+ data)
- Shell dispatcher: busybox_main() → lang_try_run() fallback
- App runtime: lang/sdk/app_runtime.c with vibe_app_* functions
- Syscalls: 20+ syscalls for graphics, input, storage, time

#### Existing libc/POSIX Stubs
- vibe_libc.h (generic)
- vibe_stdio.h (minimal FILE, printf family)
- vibe_stdlib.h (malloc, exit, atoi, etc)
- vibe_string.h (strlen, strcmp, strcpy, etc)
- BUT: These are in lang/include/ - designed for QuickJS at first

#### VFS Status
- kernel/fs/vfs.c delegates to ramfs_*
- open() / read() / write() / close() syscalls exist
- App can call vibe_app_read_file() through app_runtime.c

#### Build System
- Makefile handles app compilation to .app format
- apps linked with app_entry.c + app_runtime.c + language_main.c
- patch_app_header.py sets correct entry points
- build_appfs.py embeds apps into boot.img

---

## PHASE 1: INFRASTRUCTURE & COMPATIBILITY LAYER

### 1.1 Directory Structure

```
compat/                    (NEW)
├── libc/
│   ├── compat.h          (main header)
│   ├── alloc.c           (malloc/free enhanced)
│   ├── string.c
│   ├── stdio.c
│   └── stdlib.c
├── posix/
│   ├── posix.h
│   ├── io.c              (open/read/write/close wrappers)
│   ├── fcntl.h           (minimal flags)
│   └── types.h           (pid_t, mode_t, etc)
├── unistd/
│   ├── unistd.h
│   ├── exit.c
│   ├── getenv.c
│   └── misc.c
└── sys/
    ├── stat.h
    ├── types.h
    └── wait.h

applications/
├── native/               (hello, lua, sectorc, etc - already exist)
└── ported/               (NEW)
    ├── include/          (common headers for ported apps)
    ├── echo/
    ├── cat/
    ├── wc/
    ├── head/
    ├── tail/
    ├── grep/
    ├── sed/
    ├── less/
    └── nano/

build/apps/               (where ported apps go)
├── echo.app              (✓)
├── cat.app               (✓)
├── wc.app                (✓)
├── head.app              (✓)
├── tail.app              (✓)
├── grep.app              (✓)
├── sed.app
├── less.app
└── nano.app
```

### 1.2 Key Functions to Implement

**MEMORY:**
- malloc, free, realloc, calloc (enhanced with better coalescing)
- memalign, posix_memalign

**STRING:**
- strlen, strcmp, strncmp, strcpy, strncpy, strcat, strncat (✓)
- strchr, strrchr, strstr, strtok, strdup (✓)
- memcpy, memmove, memset, memcmp (✓)

**STDIO (Critical for text apps):**
- printf, snprintf, vsnprintf (formatting)
- puts, putchar, getchar
- fopen, fclose, fread, fwrite (FILE* operations)
- fprintf, fscanf (FILE operations)
- stdin, stdout, stderr redirection

**STDLIB:**
- atoi, atol, strtol, strtoll, strtoul (✓)
- abs, labs, llabs
- exit, abort, atexit
- rand, srand
- qsort, bsearch

**POSIX I/O (Essential for file-based apps):**
- open, close, read, write
- lseek, stat, fstat, isatty
- dup, dup2

**PROCESS:**
- exit(code)
- getpid()
- getenv(name) - stub/fake if needed

**UTILITY:**
- assert
- errno handling

---

## PHASE 2: PORT ECHO & CAT (✓ DONE)

### 2.1 Source Fetching
```
coreutils-9.1 tar from gnu.org
- extract echo sources
- extract cat sources
- minimal POSIX layer required
```

### 2.2 Build Flow
```
1. gcc (host) to verify compile
2. i686-elf-gcc to cross-compile
3. Create app_entry + app_runtime + echo_main.c 
4. Link to .app binary
5. embed in boot.img
6. test in QEMU
```

### 2.3 Integration
```
shell dispatcher:
  if (cmd == "echo") → try external app first
  elif (cmd == "cat") → try external app first
  else (fallback to fake if external not found)
```

---

## PHASE 3: PORT WC, HEAD, TAIL, GREP (✓ DONE)

Similar pattern to echo/cat.

---

## PHASE 4: PORT SED, LESS

More complex; require better terminal/regex support.

---

## PHASE 5: PORT NANO

Last priority; needs robust terminal & input handling.

---

## VALIDATION CHECKLIST

For each phase:

```
Boot:       ✓ System still boots
Shell:      ✓ Shell still works  
Desktop:    ✓ startx still works
Runtimes:   ✓ Lua/external langs work
Kernel:     ✓ kernel.bin size OK
Userland:   ✓ userland main not bloated
App:        ✓ App runs in QEMU
Command:    ✓ Shell can call it
Replacement:✓ Fake replaced when ready
```

---

## Current Measurements (Baseline)

Kernel size:
```
$ ls -lh build/kernel.bin
```

Userland size:
```
$ ls -lh build/kernel.bin  (includes userland)
```

Shell commands that are FAKE:
- echo: 7 lines
- cat: 10 lines
- clear: uses sys_clear()
- pwd/ls/cd/mkdir/touch/rm: filesystem ops

---

## GNU Coreutils Porting Notes

### echo
- Simple: outputs arguments separated by spaces
- GNU flags: -n (no newline), -e (interpret escapes), -E (no escapes)
- Minimal compat needed: argv loop, putchar

### cat
- Read file(s), output to stdout
- GNU flags: -n (line numbers), -v (visible), -A (all), etc
- Need: open/read/write, FILE operations, line buffering

### wc
- Count lines, words, bytes
- GNU flags: -l, -w, -c, -m
- Need: regex? No - just character/word/line counting

### head/tail
- Output first/last N lines
- Need: line buffering, seek

### grep
- Pattern matching (regex required!)
- Need: POSIX regex library (gnulib?)

### sed
- Stream editor (complex!)
- Need: Full regex + state machine

### less
- Pager (requires fullscreen terminal)
- Need: Cursor control, page buffering

### nano
- Editor (requires fullscreen + editing)
- Need: Full terminal control

---

## Risk Assessment

### SAFE (Low risk)
- echo, cat, wc (simple text processing)
- head, tail (line-based)

### MEDIUM (Some terminal dependency)
- grep (regex needed for pattern matching)
- sed (complex state machine)

### HIGH (Terminal-dependent)
- less, nano (need robust terminal)

### MITIGATION
- Build in order
- Test each in QEMU before moving to next
- Fallback to fake if external app crashes
- Keep fake implementations as safety net

