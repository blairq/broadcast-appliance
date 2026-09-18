/* ============================================================================
 * src/gateway/net/lwip-port/lwipopts.h — Configuracion de lwIP para el tunel
 *
 * Pila IP en espacio de usuario sobre WireGuard (IpStack.cpp):
 *   - NO_SYS=1: API "raw" sin threads propios de lwIP. UN solo thread (el de
 *     IpStack) es dueño de toda la pila; nadie mas la toca.
 *   - Interfaz punto a punto (el tunel): sin Ethernet/ARP/DHCP.
 *   - MTU 1420 (el de WireGuard) -> MSS 1380. Ventanas grandes con escalado
 *     para no limitar el ancho de banda de la señalizacion/HTTP.
 * ========================================================================== */
#ifndef BA_LWIPOPTS_H
#define BA_LWIPOPTS_H

#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_NETIF_API 0

#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 0
#define LWIP_ETHERNET 0
#define LWIP_DHCP 0
#define LWIP_AUTOIP 0
#define LWIP_ACD 0
#define LWIP_IGMP 0
#define LWIP_DNS 0
#define LWIP_ICMP 1
#define LWIP_RAW 0
#define LWIP_UDP 1
#define LWIP_TCP 1
#define LWIP_TCP_KEEPALIVE 1
#define LWIP_NETIF_STATUS_CALLBACK 0
#define LWIP_NETIF_LINK_CALLBACK 0
#define LWIP_STATS 0
#define LWIP_TIMERS 1
#define IP_FORWARD 0
#define IP_REASSEMBLY 1
#define IP_FRAG 1

/* Memoria: el heap propio de lwIP (MEM_SIZE) y pools. */
#define MEM_ALIGNMENT 8
#define MEM_SIZE (8 * 1024 * 1024)
#define MEMP_NUM_PBUF 512
#define MEMP_NUM_UDP_PCB 160 /* rango de puertos WebRTC + extras */
#define MEMP_NUM_TCP_PCB 128
#define MEMP_NUM_TCP_PCB_LISTEN 8
#define MEMP_NUM_TCP_SEG 1024
#define MEMP_NUM_REASSDATA 32
#define IP_REASS_MAX_PBUFS 64
#define MEMP_NUM_SYS_TIMEOUT 16
#define PBUF_POOL_SIZE 1024
#define PBUF_POOL_BUFSIZE 1600

/* TCP */
#define TCP_MSS 1380
#define LWIP_WND_SCALE 1
#define TCP_RCV_SCALE 3
#define TCP_WND (256 * 1024)
#define TCP_SND_BUF (256 * 1024)
#define TCP_SND_QUEUELEN (4 * TCP_SND_BUF / TCP_MSS)
/* Umbral de "hay lugar para escribir" (tcp_sndbuf): tiene que entrar en u16. */
#define TCP_SNDLOWAT 32768
#define TCP_QUEUE_OOSEQ 1
#define TCP_LISTEN_BACKLOG 1
#define LWIP_TCP_SACK_OUT 1

/* Checksums: se calculan y verifican (el tunel no los hace por nosotros). */
#define CHECKSUM_GEN_IP 1
#define CHECKSUM_GEN_UDP 1
#define CHECKSUM_GEN_TCP 1
#define CHECKSUM_GEN_ICMP 1
#define CHECKSUM_CHECK_IP 1
#define CHECKSUM_CHECK_UDP 1
#define CHECKSUM_CHECK_TCP 1

/* ISN de TCP impredecibles (RFC 6528): semilla aleatoria del proceso. */
#define LWIP_RAND() ba_lwip_rand()
#ifdef __cplusplus
extern "C" {
#endif
unsigned int ba_lwip_rand(void);
#ifdef __cplusplus
}
#endif

/* htons/htonl son del sistema: si lwIP los redefine como macros choca con
 * <arpa/inet.h> / <winsock2.h> en IpStack.cpp. lwIP usa lwip_htons igual. */
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS 1

#define LWIP_DEBUG 0

#endif /* BA_LWIPOPTS_H */
