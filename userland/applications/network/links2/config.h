#define HAVE_STRING_H 1
/* Upstream, configure injects VERSION and DEBUGLEVEL into config.h; the
 * hand-maintained vendored copy omitted both, so setup.h's VERSION_STRING
 * and menu.c's about-box (DEBUGLEVEL) failed to compile. Release default
 * (config-vibeos.h: "2.29", DEBUG undefined). */
#define VERSION "2.29"
#define DEBUGLEVEL 0
#define HAVE_STRINGS_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDLIB_H_X 1
#define HAVE_STDINT_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDARG_H 1
#define HAVE_CTYPE_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_IOCTL_H 1
/* links.h includes <sys/wait.h> only when this is defined; the compat tree
 * ships the header (wait/waitpid + W macros) so links.c's select.c reaper
 * got an implicit-decl instead of a declaration. */
#define HAVE_SYS_WAIT_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_NETDB_H 1
#define HAVE_ARPA_INET_H 1
#define HAVE_SYS_UN_H 1
#define HAVE_UNISTD_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_SETJMP_H 1
#define HAVE_TIME_H 1
#define HAVE_LOCALE_H 1
#define HAVE_LIMITS_H 1
#define STDC_HEADERS 1
#define HAVE_VPRINTF 1
#define HAVE_STRFTIME 1
#define HAVE_MEMSET 1
#define HAVE_STRDUP 1
#define HAVE_STRSTR 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRNCMP 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMCMP 1
#define HAVE_SNPRINTF 1
#define HAVE_VSNPRINTF 1
#define HAVE_SELECT 1
#define HAVE_POLL 1
#define HAVE_SOCKET 1
#define HAVE_CONNECT 1
#define HAVE_BIND 1
#define HAVE_LISTEN 1
#define HAVE_ACCEPT 1
#define HAVE_SEND 1
#define HAVE_RECV 1
#define HAVE_GETADDRINFO 1
#define HAVE_GETHOSTBYNAME 1
#define HAVE_GETNAMEINFO 1
#define HAVE_INET_ATON 1
#define HAVE_INET_NTOP 1
#define HAVE_FCNTL 1
#define HAVE_IOCTL 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_USLEEP 1
#define HAVE_CHDIR 1
#define HAVE_GETCWD 1
#define HAVE_OPENDIR 1
#define HAVE_READDIR 1
#define HAVE_CLOSEDIR 1
#define HAVE_STAT 1
#define HAVE_FSTAT 1
#define HAVE_ACCESS 1
#define HAVE_MKDIR 1
#define HAVE_RMDIR 1
#define HAVE_UNLINK 1
#define HAVE_RENAME 1
#define HAVE_DUP 1
#define HAVE_DUP2 1
#define HAVE_GETPID 1
#define HAVE_PIPE 1
#define HAVE_ISATTY 1
#define HAVE_TTYNAME 1
#define HAVE_GETPWUID 1
#define HAVE_GETPWNAM 1
#define HAVE_GETGRGID 1
#define HAVE_GETGRNAM 1
#define HAVE_QSORT 1
#define HAVE_BZERO 1
#define HAVE_INDEX 1
#define HAVE_RINDEX 1
#define HAVE_ALLOCA 1
#define HAVE_ALLOCA_H 1
#define HAVE_STRERROR 1
#define HAVE_STRTOUL 1
#define HAVE_STRTOLL 1
#define HAVE_STRTOD 1
#define HAVE_ATOI 1
#define HAVE_ATOL 1
#define HAVE_ATOF 1
#define HAVE_RAND 1
#define HAVE_SRAND 1
#define HAVE_EXIT 1
#define HAVE_ABORT 1
#define HAVE_SETBUF 1
#define HAVE_SETLINEBUF 1
#define HAVE_GETPASS 1
#define HAVE_POPEN 1
#define HAVE_PCLOSE 1
#define HAVE_TMPFILE 1
#define HAVE_MKSTEMP 1
#define TIME_WITH_SYS_TIME 1
#define RETSIGTYPE void
#define SIZEOF_UNSIGNED 4
#define SIZEOF_UNSIGNED_LONG 4
#define SIZEOF_UNSIGNED_LONG_LONG 8
#define SIZEOF_UNSIGNED_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_SHORT 2
#define SIZEOF_CHAR 1
#define SIZEOF_VOID_P 4
#define SIZEOF_OFF_T 4
#define SIZEOF_TIME_T 4
#define HAVE_LRAND48 1
#define HAVE_SRAND48 1
#define HAVE_DRAND48 1
#define HAVE_GETLOADAVG 1
#define HAVE_UNAME 1
#define HAVE_GETENV 1
#define HAVE_SETENV 1
#define HAVE_PUTENV 1
#define HAVE_SYSTEM 1
#define HAVE_FORK 1
#define HAVE_EXECVP 1
#define HAVE_EXECVPE 1
#define HAVE_WAITPID 1
#define HAVE_WAIT3 1
#define HAVE_SIGACTION 1
#define HAVE_SIGPROCMASK 1
#define HAVE_SIGPENDING 1
#define HAVE_SIGSETJMP 1
#define HAVE_ALARM 1
#define HAVE_PAUSE 1
#define HAVE_SLEEP 1
#define HAVE_DIRENT_H 1
#define HAVE_PWD_H 1
#define HAVE_GRP_H 1
#define HAVE_TERMIOS_H 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_MMAP 1
#define HAVE_SHMAT 1
#undef HAVE_OPENPTY
#undef HAVE_GRX
#undef HAVE_LIBGRX20
#undef HAVE_LIBGRX20DLL
#undef HAVE_LIBGPM
#undef HAVE_LIBSSL
#undef HAVE_LIBCRYPTO
#undef HAVE_OPENSSL_SSL_H
#undef HAVE_OPENSSL_EVP_H
#undef HAVE_OPENSSL_PEM_H
#undef HAVE_OPENSSL_ERR_H
#undef HAVE_OPENSSL_RAND_H
#undef HAVE_OPENSSL_SHA_H
#undef HAVE_OPENSSL_ECDH_H
#undef HAVE_OPENSSL_ECDSA_H
#undef HAVE_OPENSSL_OBJECTS_H
#undef HAVE_OPENSSL_X509_H
#undef HAVE_TIFF
#undef HAVE_LIBTIFF
#undef HAVE_LIBPNG
#undef HAVE_PNG_H
#undef HAVE_LIBZ
#undef HAVE_ZLIB_H
#undef HAVE_LIBJPEG
#undef HAVE_JPEG_H
#undef HAVE_LIBGIF
#undef HAVE_GIF_H
#undef HAVE_LIBWEBP
#undef HAVE_WEBP_H
#undef HAVE_LIBAVIF
#undef HAVE_AVIF_H
#undef HAVE_LIBSVGA
#undef HAVE_LIBVGA
#undef HAVE_LIBVGA_CONFIG
#undef HAVE_DIRECTFB
#undef HAVE_FONTCONFIG
#undef HAVE_FREETYPE
#undef HAVE_LIBFREETYPE
#undef HAVE_FT2BUILD_H
#undef HAVE_FRIBIDI
#undef HAVE_LIBFRIBIDI
#undef HAVE_IPV6
#undef HAVE_GETHOSTBYNAME2
#undef HAVE_SOCKS5
#define HAVE_AF_UNIX 1
#define HAVE_SYS_UN_H 1
#undef HAVE_PTHREAD
#undef HAVE_PTHREADS
#undef HAVE_FORK_IS_THREAD
#define OS_UNIX 1
#define HAVE_STRLCAT 1
#define HAVE_STRLCPY 1
#define NO_IE 1
/* Terminal port (Build.links2.mk runs WITHOUT -DG): the vibeos graphics
 * driver drags in GRDRV_VIRTUAL_DEVICES and the png-based glyph pipeline,
 * and this tree vendors no libpng. Re-enable only together with a G-mode
 * build + png backend. */
#undef GRDRV_VIBEOS
