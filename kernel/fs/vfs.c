#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>
#include <kernel/fs.h>

/* current VFS only delegates to ramfs; in future we'll support multiple
   filesystems and mount points. */

extern void ramfs_init(void);
extern int ramfs_create(const char *name, const void *data, size_t size);
extern int ramfs_open(const char *path);
extern int ramfs_read(int fd, void *buf, size_t count);
extern int ramfs_write(int fd, const void *buf, size_t count);
extern int ramfs_close(int fd);
extern off_t ramfs_lseek(int fd, off_t offset, int whence);
extern int ramfs_stat(const char *path, struct stat *buf);
extern int ramfs_fstat(int fd, struct stat *buf);

/* initialize backend and seed a small rootfs */
void vfs_init(void) {
    static const char readme[] = "VibeOS ramfs\n";
    ramfs_init();
    /* ignore failures; files are optional */
    (void)ramfs_create("README", readme, sizeof(readme) - 1);
}

int open(const char *path, int flags) {
    (void)flags;
    return ramfs_open(path);
}

int read(int fd, void *buf, size_t count) {
    return ramfs_read(fd, buf, count);
}

int write(int fd, const void *buf, size_t count) {
    return ramfs_write(fd, buf, count);
}

int close(int fd) {
    return ramfs_close(fd);
}

off_t lseek(int fd, off_t offset, int whence) {
    return ramfs_lseek(fd, offset, whence);
}

int stat(const char *path, struct stat *buf) {
    return ramfs_stat(path, buf);
}

int fstat(int fd, struct stat *buf) {
    return ramfs_fstat(fd, buf);
}
