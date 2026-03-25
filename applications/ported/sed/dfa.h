#ifndef VIBE_SED_DFA_H
#define VIBE_SED_DFA_H

#include <stdbool.h>

#include "idx.h"

#define DFA_EOL_NUL 1

struct localeinfo;

struct dfa {
  int placeholder;
};

struct dfa *dfaalloc(void);
void dfasyntax(struct dfa *dfa, struct localeinfo const *info, int syntax, int opts);
void dfacomp(char const *pattern, idx_t len, struct dfa *dfa, bool searchflag);
char *dfaexec(struct dfa *dfa, char const *begin, char *end, bool allow_nl, idx_t *count, bool *backref);
struct dfa *dfasuperset(struct dfa const *dfa);
bool dfaisfast(struct dfa const *dfa);
void dfafree(struct dfa *dfa);

#endif
