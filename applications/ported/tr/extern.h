typedef struct {
    enum { STRING1, STRING2 } which;
    enum { EOS, INFINITE, NORMAL, RANGE, SEQUENCE, SET } state;
    int cnt;
    int lastch;
    int equiv[2];
    int *set;
    unsigned char *str;
} STR;

#include <limits.h>
#define NCHARS (UCHAR_MAX + 1)
#define OOBCH  (UCHAR_MAX + 1)

int next(STR *s);
