#ifndef VIBE_ARPA_INET_H
#define VIBE_ARPA_INET_H

#include <netinet/in.h>
#include <sys/socket.h>

in_addr_t inet_addr(const char *cp);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
int inet_pton(int af, const char *src, void *dst);
char *inet_ntoa(struct in_addr in);

#endif
