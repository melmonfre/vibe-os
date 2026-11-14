#ifndef SYS_SOCKET_H
#define SYS_SOCKET_H
#include <sys/types.h>
struct sockaddr {
	sa_family_t sa_family;
	char sa_data[14];
};
struct iovec {
	void *iov_base;
	size_t iov_len;
};
struct msghdr {
	void *msg_name;
	socklen_t msg_namelen;
	struct iovec *msg_iov;
	int msg_iovlen;
	void *msg_control;
	socklen_t msg_controllen;
	int msg_flags;
};
extern int socket(int, int, int);
extern int connect(int, const struct sockaddr *, socklen_t);
extern int bind(int, const struct sockaddr *, socklen_t);
extern int listen(int, int);
extern int accept(int, struct sockaddr *, socklen_t *);
extern ssize_t send(int, const void *, size_t, int);
extern ssize_t recv(int, void *, size_t, int);
extern ssize_t sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
extern ssize_t recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
extern int setsockopt(int, int, int, const void *, socklen_t);
extern int getsockopt(int, int, int, void *, socklen_t *);
extern int shutdown(int, int);
extern int getpeername(int, struct sockaddr *, socklen_t *);
extern int getsockname(int, struct sockaddr *, socklen_t *);
#define AF_UNIX 1
#define AF_INET 2
#define AF_INET6 10
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
/*
 * Option numbers MUST equal the system-published values in
 * headers/sys/socket.h: libcompat's getsockopt/setsockopt switches were
 * compiled against those headers, and a vendored tree using BSD-style
 * numbers (SOL_SOCKET 1, SO_ERROR 4) fell through to ENOPROTOOPT - the
 * measured -42 that killed links2's connected() (KNOWN_BUGS #34).
 */
#define SOL_SOCKET 0xffff
#define SO_DEBUG 0x0001
#define SO_ACCEPTCONN 0x0002
#define SO_REUSEADDR 0x0004
#define SO_KEEPALIVE 0x0008
#define SO_DONTROUTE 0x0010
#define SO_BROADCAST 0x0020
#define SO_LINGER 0x0080
#define SO_OOBINLINE 0x0100
#define SO_REUSEPORT 0x0200
#define SO_SNDBUF 0x1001
#define SO_RCVBUF 0x1002
#define SO_SNDTIMEO 0x1005
#define SO_RCVTIMEO 0x1006
#define SO_ERROR 0x1007
#define SO_TYPE 0x1008
#define SO_DOMAIN 0x1024
#define SO_PROTOCOL 0x1025
#endif
