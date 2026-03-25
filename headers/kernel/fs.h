#ifndef KERNEL_FS_H
#define KERNEL_FS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

/* very simple filesystem API currently backed by an in‑memory ramfs */

/* open a file; returns a non‑negative file descriptor or -1 on error */
int open(const char *path, int flags);

/* read up to count bytes from fd into buf; returns number of bytes read */
int read(int fd, void *buf, size_t count);

/* write up to count bytes from buf into fd; returns number of bytes written */
int write(int fd, const void *buf, size_t count);

/* close a descriptor */
int close(int fd);

/* reposition the descriptor offset */
off_t lseek(int fd, off_t offset, int whence);

/* query metadata by path or descriptor */
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);

/* initialize the virtual filesystem layer (ramfs backend for now) */
void vfs_init(void);

#endif /* KERNEL_FS_H */
