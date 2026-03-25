#ifndef VIBE_SED_SELINUX_H
#define VIBE_SED_SELINUX_H

typedef char *security_context_t;

static inline int is_selinux_enabled(void) { return 0; }
static inline int lgetfilecon(const char *path, security_context_t *con) { (void) path; (void) con; return -1; }
static inline int getfscreatecon(security_context_t *con) { (void) con; return -1; }
static inline int setfscreatecon(security_context_t con) { (void) con; return 0; }
static inline void freecon(security_context_t con) { (void) con; }

#endif
