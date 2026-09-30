/* cmake/microdns-posix/config.h — config.h de libmicrodns para macOS (y para
 * Linux sin obs-deps): lo que su meson.build detecta en un sistema POSIX. */
#define HAVE_POLL 1
#define HAVE_STRUCT_POLLFD 1
#define HAVE_GETIFADDRS 1
#define HAVE_IFADDRS_H 1
#define HAVE_UNISTD_H 1
#define HAVE_INET_NTOP 1
