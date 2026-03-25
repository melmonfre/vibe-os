#include "dfa.h"

#include <stdlib.h>

struct dfa *
dfaalloc(void)
{
  return calloc(1, sizeof(struct dfa));
}

void
dfasyntax(struct dfa *dfa, struct localeinfo const *info, int syntax, int opts)
{
  (void) dfa;
  (void) info;
  (void) syntax;
  (void) opts;
}

void
dfacomp(char const *pattern, idx_t len, struct dfa *dfa, bool searchflag)
{
  (void) pattern;
  (void) len;
  (void) dfa;
  (void) searchflag;
}

char *
dfaexec(struct dfa *dfa, char const *begin, char *end, bool allow_nl, idx_t *count, bool *backref)
{
  (void) dfa;
  (void) begin;
  (void) end;
  (void) allow_nl;
  (void) count;
  if (backref)
    *backref = true;
  return NULL;
}

struct dfa *
dfasuperset(struct dfa const *dfa)
{
  (void) dfa;
  return NULL;
}

bool
dfaisfast(struct dfa const *dfa)
{
  (void) dfa;
  return false;
}

void
dfafree(struct dfa *dfa)
{
  (void) dfa;
}
