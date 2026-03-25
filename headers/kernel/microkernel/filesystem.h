#ifndef KERNEL_MICROKERNEL_FILESYSTEM_H
#define KERNEL_MICROKERNEL_FILESYSTEM_H

#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>

struct mk_fs_open_request {
    uint32_t flags;
    uint32_t path_length;
    uint32_t path_transfer_id;
};

struct mk_fs_io_request {
    int32_t fd;
    uint32_t size;
    uint32_t transfer_id;
};

struct mk_fs_close_request {
    int32_t fd;
};

struct mk_fs_seek_request {
    int32_t fd;
    int32_t offset;
    int32_t whence;
};

struct mk_fs_stat_request {
    uint32_t path_length;
    uint32_t path_transfer_id;
    uint32_t stat_transfer_id;
};

struct mk_fs_fstat_request {
    int32_t fd;
    uint32_t stat_transfer_id;
};

struct mk_fs_result {
    int32_t value;
};

void mk_filesystem_service_init(void);
int mk_filesystem_service_open(const char *path, int flags);
int mk_filesystem_service_read(int fd, void *buf, uint32_t count);
int mk_filesystem_service_write(int fd, const void *buf, uint32_t count);
int mk_filesystem_service_close(int fd);
off_t mk_filesystem_service_lseek(int fd, off_t offset, int whence);
int mk_filesystem_service_stat(const char *path, struct stat *buf);
int mk_filesystem_service_fstat(int fd, struct stat *buf);

#endif
