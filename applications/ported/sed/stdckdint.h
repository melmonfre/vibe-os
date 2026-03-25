#ifndef VIBE_SED_STDCKDINT_H
#define VIBE_SED_STDCKDINT_H

#define ckd_add(result, a, b) __builtin_add_overflow((a), (b), (result))
#define ckd_mul(result, a, b) __builtin_mul_overflow((a), (b), (result))

#endif
