/* ============================================================================
 * src/gateway/net/lwip-port/arch/cc.h — Plataforma de lwIP (C99, portable)
 * ========================================================================== */
#ifndef BA_LWIP_ARCH_CC_H
#define BA_LWIP_ARCH_CC_H

#include <stdio.h>
#include <stdlib.h>

#define LWIP_PLATFORM_DIAG(x)                                                                                \
    do {                                                                                                     \
        printf x;                                                                                            \
    } while (0)
#define LWIP_PLATFORM_ASSERT(x)                                                                              \
    do {                                                                                                     \
        fprintf(stderr, "lwip: aserto '%s' en %s:%d\n", x, __FILE__, __LINE__);                              \
        abort();                                                                                             \
    } while (0)

#if defined(_MSC_VER)
#define LWIP_NO_UNISTD_H 1
#endif

#endif /* BA_LWIP_ARCH_CC_H */
