#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>
#include <kernel/fs.h>
#include <kernel/kernel.h>
#include <kernel/memory/heap.h>
#include <sys/stat.h>

#define RAMFS_MAX_FILES 16
#define RAMFS_NAME_LEN   32
#define RAMFS_MAX_FDS    32
#define RAMFS_SEEK_SET   0
#define RAMFS_SEEK_CUR   1
#define RAMFS_SEEK_END   2

typedef struct {
    char name[RAMFS_NAME_LEN];
    uint8_t *data;
    size_t size;
} ramfs_node_t;

typedef struct {
    ramfs_node_t *node;
    size_t pos;
    int in_use;
} ramfs_fd_t;

static ramfs_node_t g_nodes[RAMFS_MAX_FILES];
static int g_node_count = 0;
static ramfs_fd_t g_fds[RAMFS_MAX_FDS];

static ramfs_node_t *ramfs_find_node(const char *path) {
    int i;

    if (path == NULL) {
        return NULL;
    }
    for (i = 0; i < g_node_count; ++i) {
        if (strcmp(g_nodes[i].name, path) == 0) {
            return &g_nodes[i];
        }
    }
    return NULL;
}

static ramfs_fd_t *ramfs_get_fd(int fd) {
    if (fd < 0 || fd >= RAMFS_MAX_FDS) {
        return NULL;
    }
    if (!g_fds[fd].in_use || g_fds[fd].node == NULL) {
        return NULL;
    }
    return &g_fds[fd];
}

static void ramfs_fill_stat(const ramfs_node_t *node, struct stat *st) {
    if (node == NULL || st == NULL) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG;
    st->st_size = (off_t)node->size;
}

void ramfs_init(void) {
    memset(g_nodes, 0, sizeof(g_nodes));
    memset(g_fds, 0, sizeof(g_fds));
    g_node_count = 0;
}

/* create a file in the ramfs; returns 0 on success */
int ramfs_create(const char *name, const void *data, size_t size) {
    if (g_node_count >= RAMFS_MAX_FILES || name == NULL) {
        return -1;
    }
    ramfs_node_t *n = &g_nodes[g_node_count++];
    strncpy(n->name, name, RAMFS_NAME_LEN - 1);
    n->name[RAMFS_NAME_LEN - 1] = '\0';
    n->data = kernel_malloc(size);
    if (!n->data) {
        g_node_count--;
        return -1;
    }
    memcpy(n->data, data, size);
    n->size = size;
    return 0;
}

int ramfs_open(const char *path) {
    ramfs_node_t *node;
    int fd;

    node = ramfs_find_node(path);
    if (node == NULL) {
        return -1;
    }

    for (fd = 0; fd < RAMFS_MAX_FDS; ++fd) {
        if (!g_fds[fd].in_use) {
            g_fds[fd].in_use = 1;
            g_fds[fd].node = node;
            g_fds[fd].pos = 0;
            return fd;
        }
    }
    return -1;
}

int ramfs_read(int fd, void *buf, size_t count) {
    ramfs_fd_t *f;
    size_t remaining;
    size_t toread;

    if (!buf) {
        return -1;
    }
    f = ramfs_get_fd(fd);
    if (f == NULL) {
        return -1;
    }
    if (f->pos > f->node->size) {
        return -1;
    }
    remaining = f->node->size - f->pos;
    toread = (count < remaining) ? count : remaining;
    if (toread > 0) {
        memcpy(buf, f->node->data + f->pos, toread);
        f->pos += toread;
    }
    return (int)toread;
}

int ramfs_write(int fd, const void *buf, size_t count) {
    /* for simplicity treat write as append; no resizing supported */
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

int ramfs_close(int fd) {
    if (fd < 0 || fd >= RAMFS_MAX_FDS)
        return -1;
    g_fds[fd].in_use = 0;
    g_fds[fd].node = NULL;
    g_fds[fd].pos = 0;
    return 0;
}

off_t ramfs_lseek(int fd, off_t offset, int whence) {
    ramfs_fd_t *f;
    long base;
    long next;

    f = ramfs_get_fd(fd);
    if (f == NULL) {
        return (off_t)-1;
    }

    switch (whence) {
    case RAMFS_SEEK_SET:
        base = 0;
        break;
    case RAMFS_SEEK_CUR:
        base = (long)f->pos;
        break;
    case RAMFS_SEEK_END:
        base = (long)f->node->size;
        break;
    default:
        return (off_t)-1;
    }

    next = base + (long)offset;
    if (next < 0 || (size_t)next > f->node->size) {
        return (off_t)-1;
    }

    f->pos = (size_t)next;
    return (off_t)f->pos;
}

int ramfs_stat(const char *path, struct stat *buf) {
    ramfs_node_t *node;

    if (buf == NULL) {
        return -1;
    }
    node = ramfs_find_node(path);
    if (node == NULL) {
        return -1;
    }
    ramfs_fill_stat(node, buf);
    return 0;
}

int ramfs_fstat(int fd, struct stat *buf) {
    ramfs_fd_t *f;

    if (buf == NULL) {
        return -1;
    }
    f = ramfs_get_fd(fd);
    if (f == NULL) {
        return -1;
    }
    ramfs_fill_stat(f->node, buf);
    return 0;
}
