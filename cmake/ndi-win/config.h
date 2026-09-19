/* cmake/ndi-win/config.h — config.h de libmicrodns para Windows (lo que su
 * meson.build detecta con clang-cl/MSVC: sin poll() propio -> compat/poll.c). */
#define HAVE_INET_NTOP 1
#define HAVE_STRUCT_POLLFD 1
