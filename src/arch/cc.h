/* lwIP platform definitions for this board: bare metal, GCC, Cortex-M4
 * (little endian, which is lwIP's default). */

#ifndef ARCH_CC_H
#define ARCH_CC_H

#include <stdint.h>

void uart_puts(const char *s);
uint32_t lwip_rand(void);

/* Use lwIP's own isdigit() etc. instead of the C library's, which pull in
 * locale tables */
#define LWIP_NO_CTYPE_H 1

/* Random numbers for DHCP transaction IDs and address conflict checks */
#define LWIP_RAND() lwip_rand()

/* Debug messages are off; a failed assertion prints and stops */
#define LWIP_PLATFORM_DIAG(x) do { } while (0)
#define LWIP_PLATFORM_ASSERT(x) do {        \
        uart_puts("lwIP assert: ");         \
        uart_puts(x);                       \
        uart_puts("\r\n");                  \
        for (;;) {                          \
        }                                   \
    } while (0)

#endif
