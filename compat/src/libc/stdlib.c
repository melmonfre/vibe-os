#include "../include/compat/libc/stdlib.h"
#include "../include/compat/libc/string.h"
#include <lang/include/vibe_app_runtime.h>
#include <stdint.h>

void *malloc(size_t size) {
    return vibe_app_malloc(size);
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    return vibe_app_realloc(ptr, size);
}

void free(void *ptr) { vibe_app_free(ptr); }

void exit(int status) { 
    // TODO: This should terminate the process
    (void)status; 
    while(1); 
}

void abort(void) { 
    // TODO: This should terminate the process
    while(1); 
}

int atoi(const char *s) {
    int result = 0;
    int sign = 1;
    
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    
    return result * sign;
}

long strtol(const char *s, char **endptr, int base) {
    long result = 0;
    int sign = 1;
    
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                base = 16;
                s++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (*s == '0' && (*(s+1) == 'x' || *(s+1) == 'X')) {
            s += 2;
        }
    }
    
    while (*s) {
        int digit = -1;
        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (*s >= 'a' && *s <= 'z') {
            digit = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'Z') {
            digit = *s - 'A' + 10;
        } else {
            break;
        }
        
        if (digit >= base) break;
        result = result * base + digit;
        s++;
    }
    
    if (endptr) *endptr = (char *)s;
    return result * sign;
}

void qsort(void *base, size_t nmemb, size_t size,
    int (*compar)(const void *, const void *)) {
    char *b = (char *)base;
    char tmp[256];
    for (size_t i = 1; i < nmemb; i++) {
        size_t j = i;
        while (j > 0 && compar(b + j * size, b + (j - 1) * size) < 0) {
            for (size_t k = 0; k < size; k++) tmp[k] = b[j * size + k];
            for (size_t k = 0; k < size; k++) b[j * size + k] = b[(j - 1) * size + k];
            for (size_t k = 0; k < size; k++) b[(j - 1) * size + k] = tmp[k];
            j--;
        }
    }
}

unsigned long strtoul(const char *s, char **endptr, int base) {
    return (unsigned long)strtol(s, endptr, base);
}

long long strtoll(const char *s, char **endptr, int base) {
    return (long long)strtol(s, endptr, base);
}

unsigned long long strtoull(const char *s, char **endptr, int base) {
    return (unsigned long long)strtoul(s, endptr, base);
}

static unsigned long next_rand = 1;
int rand(void) {
    next_rand = next_rand * 1103515245 + 12345;
    return (unsigned int)(next_rand / 65536) % 32768;
}
void srand(unsigned int seed) { next_rand = seed; }
void srand_deterministic(unsigned int seed) { srand(seed); }

/* abs, labs, llabs, div, ldiv, lldiv are in desktop_math_compat.c */

/*
 * strtod(): this libc had none, so any program that asked for it silently
 * fell back to vendor-side replacements that themselves needed sscanf
 * (also absent) — links2's font/HTTP numeric parsing was the first victim.
 * Covers sign, integer, fraction and decimal exponent with saturation at
 * the decade limit; a non-numeric prefix returns 0 with endptr==s, per
 * the C standard.
 */
double strtod(const char *s, char **endptr) {
    const char *p = s;
    double sign = 1.0;
    double value = 0.0;
    double divisor = 1.0;
    int seen = 0;

    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) {
        ++p;
    }
    if (*p == '-') {
        sign = -1.0;
        ++p;
    } else if (*p == '+') {
        ++p;
    }
    while (*p >= '0' && *p <= '9') {
        value = value * 10.0 + (double)(*p - '0');
        seen = 1;
        ++p;
    }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            divisor *= 10.0;
            value += (double)(*p - '0') / divisor;
            seen = 1;
            ++p;
        }
    }
    if (!seen) {
        if (endptr) {
            *endptr = (char *)s;
        }
        return 0.0;
    }
    if ((*p == 'e' || *p == 'E')) {
        const char *pe = p + 1;
        int eneg = 0, e = 0, eseen = 0;
        if (*pe == '-') {
            eneg = 1;
            ++pe;
        } else if (*pe == '+') {
            ++pe;
        }
        while (*pe >= '0' && *pe <= '9') {
            if (e < 308) {
                e = e * 10 + (*pe - '0');
                if (e > 308) {
                    e = 308;
                }
            }
            eseen = 1;
            ++pe;
        }
        if (eseen) {
            int i;
            p = pe;
            for (i = 0; i < e; ++i) {
                if (eneg) {
                    value /= 10.0;
                } else {
                    value *= 10.0;
                }
            }
        }
    }
    if (endptr) {
        *endptr = (char *)p;
    }
    return sign * value;
}

/* atof(): declared by stdlib.h, implemented on top of the strtod above. */
double atof(const char *s) {
    return strtod(s, (char **)0);
}
