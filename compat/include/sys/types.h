/*
 * POSIX System Types
 * Minimal definitions for VibeOS
 */

#ifndef COMPAT_SYS_TYPES_H
#define COMPAT_SYS_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* File descriptor type */
typedef int32_t fd_t;

/* Process ID */
typedef int32_t pid_t;

/* User/Group IDs (fake for now) */
typedef int32_t uid_t;
typedef int32_t gid_t;

/* File mode */
typedef uint32_t mode_t;

/* File size/offset */
typedef int64_t off_t;

/* Time type (seconds since epoch) */
typedef int64_t time_t;

/* Clock ticks */
typedef uint32_t clock_t;

/* Size types - signed version of size_t */
typedef int32_t ssize_t;

/* Device/inode types */
typedef uint32_t dev_t;
typedef uint32_t ino_t;

/* Link count */
typedef uint32_t nlink_t;

/* Permissions */
#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRGRP  0040
#define S_IWGRP  0020
#define S_IXGRP  0010
#define S_IROTH  0004
#define S_IWOTH  0002
#define S_IXOTH  0001

#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFBLK  0060000

#define S_ISREG(m) (((m) & 0170000) == 0100000)
#define S_ISDIR(m) (((m) & 0170000) == 0040000)
#define S_ISCHR(m) (((m) & 0170000) == 0020000)
#define S_ISBLK(m) (((m) & 0170000) == 0060000)

#endif /* COMPAT_SYS_TYPES_H */
