#ifndef VIBE_BSDGAME_SETJMP_H
#define VIBE_BSDGAME_SETJMP_H

typedef int jmp_buf[5];

static inline __attribute__((noreturn)) void vibe_bsdgame_setjmp_longjmp(int *env, int val) {
    (void)val;
    __builtin_longjmp(env, 1);
}

#define setjmp(env) __builtin_setjmp((int *)(env))
#define longjmp(env, val) vibe_bsdgame_setjmp_longjmp((int *)(env), (val))

#endif
