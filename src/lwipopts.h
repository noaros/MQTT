/* lwIP configuration. Anything not set here keeps its default from
 * lib/lwip/src/include/lwip/opt.h. */

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* Bare metal: no OS, no threads. The main loop calls sys_check_timeouts(),
 * and the only thing lwIP needs from us is sys_now(). */
#define NO_SYS                  1
#define SYS_LIGHTWEIGHT_PROT    0  /* lwIP is never called from interrupts */
#define LWIP_NETCONN            0  /* these two APIs need an OS */
#define LWIP_SOCKET             0

/* Memory: a heap for outgoing packets, a pool of buffers for incoming
 * ones, each big enough for a full Ethernet frame */
#define MEM_ALIGNMENT           4
#define MEM_SIZE                (8 * 1024)
#define PBUF_POOL_SIZE          8
#define PBUF_POOL_BUFSIZE       1536

/* Protocols: IPv4 with ARP, ICMP (ping), UDP and DHCP, and TCP for MQTT */
#define LWIP_ARP                1
#define LWIP_ICMP               1
#define LWIP_UDP                1
#define LWIP_DHCP               1
#define LWIP_TCP                1

/* TCP sizes. MSS (maximum segment size) = the most data per segment:
 * a 1500-byte IP packet minus 20 bytes each of IP and TCP header. The
 * send buffer and receive window hold two full segments each, plenty
 * for small MQTT messages. */
#define TCP_MSS                 1460
#define TCP_SND_BUF             (2 * TCP_MSS)
#define TCP_WND                 (2 * TCP_MSS)

#define LWIP_STATS              0

/* Call us when the IP address changes, to print the one DHCP got */
#define LWIP_NETIF_STATUS_CALLBACK  1

#endif
