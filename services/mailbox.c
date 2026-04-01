#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

static int sock;
static int conn = -1;

int mailbox_init(const char *path) {
    unlink(path);
    int lsock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (lsock < 0) return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);
    if (bind(lsock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(lsock); return -2; }
    listen(lsock, 4);
    sock = lsock;
    return 0;
}

static void accept_new(void) {
    if (conn >= 0) return;
    conn = accept(sock, NULL, NULL);
    if (conn < 0) conn = -1;
}

int recv_incoming(void *req) {
    accept_new();
    if (conn < 0) return -1;
    ssize_t n = recv(conn, req, 128, 0);
    if (n <= 0) { close(conn); conn = -1; return -1; }
    return 0;
}

void send_reply(void *rep) {
    if (conn < 0) return;
    send(conn, rep, 8, 0); /* assume sizeof reply ≤ 8 */
}

void mailbox_fini(void) {
    if (conn >= 0) close(conn);
    close(sock);
}