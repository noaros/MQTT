/* Bare-metal temperature readout for Nucleo-F429ZI.
 *
 * Reads the internal temperature sensor once per second and prints it on
 * USART3 (PD8 = TX), which the Nucleo routes to the ST-LINK virtual COM
 * port. Open the port at 115200 8N1. Also reads the Ethernet PHY's ID over
 * MDIO at startup and reports whenever the link goes up or down.
 *
 * Runs the lwIP network stack on top of an Ethernet driver that moves
 * frames to and from the MAC by DMA. The board gets its IP address with
 * DHCP and answers ping.
 *
 * Clock: 168 MHz from the PLL, fed by the 8 MHz clock the ST-LINK supplies
 * on the HSE input. Falls back to HSI = 16 MHz if that clock is missing. */

#include <stdint.h>

#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#define REG(addr) (*(volatile uint32_t *)(addr))

/* RCC (reference manual RM0090 section 6.3) */
#define RCC_BASE      0x40023800
#define RCC_CR        REG(RCC_BASE + 0x00)
#define RCC_PLLCFGR   REG(RCC_BASE + 0x04)
#define RCC_CFGR      REG(RCC_BASE + 0x08)
#define RCC_AHB1ENR   REG(RCC_BASE + 0x30)
#define RCC_APB1ENR   REG(RCC_BASE + 0x40)
#define RCC_APB2ENR   REG(RCC_BASE + 0x44)

/* SYSCFG (RM0090 section 9.2) */
#define SYSCFG_PMC    REG(0x40013800 + 0x04)

/* GPIOA, GPIOB, GPIOC, GPIOG (RM0090 section 8.4) */
#define GPIOA_BASE    0x40020000
#define GPIOA_MODER   REG(GPIOA_BASE + 0x00)
#define GPIOA_OSPEEDR REG(GPIOA_BASE + 0x08)
#define GPIOA_AFRL    REG(GPIOA_BASE + 0x20)
#define GPIOC_BASE    0x40020800
#define GPIOC_MODER   REG(GPIOC_BASE + 0x00)
#define GPIOC_OSPEEDR REG(GPIOC_BASE + 0x08)
#define GPIOC_AFRL    REG(GPIOC_BASE + 0x20)
#define GPIOB_BASE    0x40020400
#define GPIOG_BASE    0x40021800

/* Flash interface (RM0090 section 3.9) */
#define FLASH_ACR     REG(0x40023C00)

/* GPIOD (RM0090 section 8.4) */
#define GPIOD_BASE    0x40020C00
#define GPIOD_MODER   REG(GPIOD_BASE + 0x00)
#define GPIOD_AFRH    REG(GPIOD_BASE + 0x24)

/* USART3 (RM0090 section 30.6) */
#define USART3_BASE   0x40004800
#define USART3_SR     REG(USART3_BASE + 0x00)
#define USART3_DR     REG(USART3_BASE + 0x04)
#define USART3_BRR    REG(USART3_BASE + 0x08)
#define USART3_CR1    REG(USART3_BASE + 0x0C)

/* ADC1 (RM0090 section 13.13) */
#define ADC1_BASE     0x40012000
#define ADC1_SR       REG(ADC1_BASE + 0x00)
#define ADC1_CR2      REG(ADC1_BASE + 0x08)
#define ADC1_SMPR1    REG(ADC1_BASE + 0x0C)
#define ADC1_SQR1     REG(ADC1_BASE + 0x2C)
#define ADC1_SQR3     REG(ADC1_BASE + 0x34)
#define ADC1_DR       REG(ADC1_BASE + 0x4C)
#define ADC_CCR       REG(0x40012300 + 0x04)  /* common to all ADCs */

/* Temperature sensor calibration values, measured by ST in production at
 * 30 C and 110 C with VDDA = 3.3 V (datasheet section 6.3.22) */
#define TS_CAL1       (*(volatile uint16_t *)0x1FFF7A2C)
#define TS_CAL2       (*(volatile uint16_t *)0x1FFF7A2E)

/* 96-bit unique device ID, programmed by ST in production (RM0090
 * section 39.1) */
#define UID           ((const volatile uint8_t *)0x1FFF7A10)

/* Ethernet MAC and DMA registers (RM0090 section 33.8) */
#define ETH_BASE      0x40028000
#define ETH_MACCR     REG(ETH_BASE + 0x0000)
#define ETH_MACFFR    REG(ETH_BASE + 0x0004)
#define ETH_MACMIIAR  REG(ETH_BASE + 0x0010)
#define ETH_MACMIIDR  REG(ETH_BASE + 0x0014)
#define ETH_MACA0HR   REG(ETH_BASE + 0x0040)
#define ETH_MACA0LR   REG(ETH_BASE + 0x0044)
#define ETH_DMABMR    REG(ETH_BASE + 0x1000)
#define ETH_DMATPDR   REG(ETH_BASE + 0x1004)
#define ETH_DMARPDR   REG(ETH_BASE + 0x1008)
#define ETH_DMARDLAR  REG(ETH_BASE + 0x100C)
#define ETH_DMATDLAR  REG(ETH_BASE + 0x1010)
#define ETH_DMASR     REG(ETH_BASE + 0x1014)
#define ETH_DMAOMR    REG(ETH_BASE + 0x1018)

/* Standard PHY registers (IEEE 802.3 clause 22) */
#define PHY_BCR       0   /* basic control */
#define PHY_BSR       1   /* basic status */
#define PHY_ID1       2
#define PHY_ID2       3
/* LAN8742A vendor-specific register (LAN8742A datasheet section 4.2) */
#define PHY_SCSR      31  /* special control/status: negotiated speed */

/* SysTick (Cortex-M4 core peripheral) */
#define SYST_CSR      REG(0xE000E010)
#define SYST_RVR      REG(0xE000E014)
#define SYST_CVR      REG(0xE000E018)

#define HSI_HZ        16000000u
#define BAUD          115200u

/* Current clock speeds, set by clock_init() */
uint32_t sysclk_hz = HSI_HZ;
uint32_t apb1_hz = HSI_HZ;

volatile uint32_t ticks_ms;

/* Switch SYSCLK to 168 MHz:
 *   HSE 8 MHz / M 4 = 2 MHz -> x N 168 = 336 MHz (VCO)
 *   / P 2 = 168 MHz SYSCLK, / Q 7 = 48 MHz (USB, RNG)
 *   AHB / 1 = 168 MHz, APB1 / 4 = 42 MHz, APB2 / 2 = 84 MHz
 * Voltage scale 1 (the reset default) allows up to 168 MHz without
 * over-drive. Returns 0 and stays on HSI if HSE never becomes ready. */
int clock_init(void) {
    /* HSE in bypass mode: the ST-LINK drives a ready-made clock into the
     * pin, there is no crystal. HSEBYP must be set while HSE is off. */
    RCC_CR |= 1u << 18;  /* HSEBYP */
    RCC_CR |= 1u << 16;  /* HSEON */
    uint32_t timeout = 100000;
    while (!(RCC_CR & (1u << 17))) {  /* wait for HSERDY */
        if (--timeout == 0) {
            RCC_CR &= ~(1u << 16);
            return 0;
        }
    }

    /* Configure the PLL (it is off after reset), keeping reserved bits */
    RCC_PLLCFGR = (RCC_PLLCFGR & ~0x0F437FFFu)
                | (7u << 24)    /* PLLQ = 7 */
                | (1u << 22)    /* PLLSRC = HSE */
                | (0u << 16)    /* PLLP = 2 */
                | (168u << 6)   /* PLLN = 168 */
                | (4u << 0);    /* PLLM = 4 */
    RCC_CR |= 1u << 24;  /* PLLON */
    while (!(RCC_CR & (1u << 25))) {  /* wait for PLLRDY */
    }

    /* Flash can't keep up at 168 MHz: 5 wait states (RM0090 table 10),
     * plus prefetch and instruction/data caches. Must be set before the
     * switch, and read back to make sure it took effect. */
    FLASH_ACR = (1u << 10) | (1u << 9) | (1u << 8) | 5u;  /* DCEN ICEN PRFTEN */
    while ((FLASH_ACR & 0xFu) != 5u) {
    }

    /* Bus prescalers: AHB / 1, APB1 / 4 (max 45 MHz), APB2 / 2 (max 90 MHz) */
    RCC_CFGR = (RCC_CFGR & ~0xFCF0u) | (4u << 13) | (5u << 10);

    /* Switch SYSCLK to the PLL */
    RCC_CFGR = (RCC_CFGR & ~3u) | 2u;  /* SW = PLL */
    while ((RCC_CFGR & (3u << 2)) != (2u << 2)) {  /* wait for SWS = PLL */
    }

    sysclk_hz = 168000000u;
    apb1_hz = 42000000u;
    return 1;
}

void SysTick_Handler(void) {
    ticks_ms++;
}

void delay_ms(uint32_t ms) {
    uint32_t start = ticks_ms;
    while (ticks_ms - start < ms) {
    }
}

void uart_init(void) {
    RCC_AHB1ENR |= 1u << 3;   /* GPIODEN */
    RCC_APB1ENR |= 1u << 18;  /* USART3EN */

    /* PD8: alternate function mode (MODER = 0b10), AF7 = USART3_TX */
    GPIOD_MODER = (GPIOD_MODER & ~(3u << 16)) | (2u << 16);
    GPIOD_AFRH  = (GPIOD_AFRH & ~(0xFu << 0)) | (7u << 0);

    /* USART3 is on APB1. Oversampling by 16: BRR = fCK / baud, rounded
     * (42 MHz / 115200 = 365) */
    USART3_BRR = (apb1_hz + BAUD / 2) / BAUD;
    USART3_CR1 = (1u << 13) | (1u << 3);  /* UE | TE */
}

void uart_putc(char c) {
    while (!(USART3_SR & (1u << 7))) {  /* wait for TXE */
    }
    USART3_DR = (uint32_t)c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

/* Print an unsigned number in decimal */
void uart_putu(uint32_t n) {
    char buf[10];
    int i = 0;
    do {
        buf[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (i) {
        uart_putc(buf[--i]);
    }
}

/* Print an 8-bit value as 2 hex digits */
void uart_puthex8(uint32_t n) {
    uart_putc("0123456789ABCDEF"[(n >> 4) & 0xFu]);
    uart_putc("0123456789ABCDEF"[n & 0xFu]);
}

/* Print a 16-bit value as 4 hex digits */
void uart_puthex16(uint32_t n) {
    uart_puthex8(n >> 8);
    uart_puthex8(n);
}

/* Print a MAC address as AA:BB:CC:DD:EE:FF */
void uart_putmac(const uint8_t *mac) {
    for (int i = 0; i < 6; i++) {
        if (i) {
            uart_putc(':');
        }
        uart_puthex8(mac[i]);
    }
}

/* Set up the Ethernet MAC just enough to use its MDIO interface:
 * MDC on PC1 and MDIO on PA2, both alternate function 11. */
void mdio_init(void) {
    RCC_AHB1ENR |= (1u << 0) | (1u << 2);  /* GPIOAEN, GPIOCEN */
    RCC_APB2ENR |= 1u << 14;               /* SYSCFGEN */

    /* Select RMII (the Nucleo's PHY connection). Must be done before the
     * MAC clock is enabled. */
    SYSCFG_PMC |= 1u << 23;  /* MII_RMII_SEL */

    RCC_AHB1ENR |= 1u << 25;  /* ETHMACEN */

    /* PA2 = MDIO, PC1 = MDC: alternate function mode, AF11, high speed */
    GPIOA_MODER   = (GPIOA_MODER & ~(3u << 4)) | (2u << 4);
    GPIOA_OSPEEDR |= 3u << 4;
    GPIOA_AFRL    = (GPIOA_AFRL & ~(0xFu << 8)) | (11u << 8);
    GPIOC_MODER   = (GPIOC_MODER & ~(3u << 2)) | (2u << 2);
    GPIOC_OSPEEDR |= 3u << 2;
    GPIOC_AFRL    = (GPIOC_AFRL & ~(0xFu << 4)) | (11u << 4);
}

/* Read a 16-bit PHY register over MDIO. A missing PHY reads as 0xFFFF. */
uint32_t mdio_read(uint32_t phy, uint32_t reg) {
    /* MDC must be <= 2.5 MHz. Clock range CR: HCLK / 102 for 150-168 MHz
     * (1.65 MHz), HCLK / 16 for 20-35 MHz, also fine on the 16 MHz
     * fallback (1 MHz). */
    uint32_t cr = sysclk_hz >= 150000000u ? 4u : 2u;

    while (ETH_MACMIIAR & 1u) {  /* wait for MB (busy) to clear */
    }
    ETH_MACMIIAR = (phy << 11) | (reg << 6) | (cr << 2) | 1u;  /* read, MB */
    while (ETH_MACMIIAR & 1u) {
    }
    return ETH_MACMIIDR & 0xFFFFu;
}

/* Write a 16-bit PHY register over MDIO */
void mdio_write(uint32_t phy, uint32_t reg, uint32_t value) {
    uint32_t cr = sysclk_hz >= 150000000u ? 4u : 2u;

    while (ETH_MACMIIAR & 1u) {
    }
    ETH_MACMIIDR = value;
    ETH_MACMIIAR = (phy << 11) | (reg << 6) | (cr << 2) | 2u | 1u;  /* MW, MB */
    while (ETH_MACMIIAR & 1u) {
    }
}

/* Find the PHY by trying all 32 addresses, return its address or -1 */
int phy_find(void) {
    for (uint32_t addr = 0; addr < 32; addr++) {
        if (mdio_read(addr, PHY_ID1) != 0xFFFFu) {
            return (int)addr;
        }
    }
    return -1;
}

/* Soft-reset the PHY, then enable and start auto-negotiation. The
 * LAN8742A takes its power-up defaults from strap pins shared with RMII
 * data lines; on this board they come up as 10 Mbit/s half duplex with
 * auto-negotiation off (BCR = 0x0000), so set it explicitly. */
void phy_init(uint32_t phy) {
    mdio_write(phy, PHY_BCR, 1u << 15);  /* soft reset */
    uint32_t start = ticks_ms;
    while (mdio_read(phy, PHY_BCR) & (1u << 15)) {  /* self-clearing */
        if (ticks_ms - start > 500) {
            break;
        }
    }
    mdio_write(phy, PHY_BCR, (1u << 12) | (1u << 9));  /* ANEN | restart AN */
}

/* Return 1 if the link is up. The link bit in the basic status register
 * latches low: after a link drop it reads 0 once even if the link is back,
 * so read it twice to get the current state. */
int phy_link_up(uint32_t phy) {
    mdio_read(phy, PHY_BSR);
    return (mdio_read(phy, PHY_BSR) >> 2) & 1u;
}

/* Negotiated mode, SCSR bits 4:2: 001 = 10 half, 101 = 10 full,
 * 010 = 100 half, 110 = 100 full. So bit 1 of the result means
 * 100 Mbit/s and bit 2 means full duplex. */
uint32_t phy_mode(uint32_t phy) {
    return (mdio_read(phy, PHY_SCSR) >> 2) & 7u;
}

void phy_print_link(uint32_t phy, int up) {
    if (!up) {
        uart_puts("link down\r\n");
        return;
    }
    uint32_t speed = phy_mode(phy);
    uart_puts("link up, ");
    uart_puts(speed & 2u ? "100" : "10");
    uart_puts(" Mbit/s ");
    uart_puts(speed & 4u ? "full" : "half");
    uart_puts(" duplex\r\n");
}

/* lwIP's clock: milliseconds since boot */
uint32_t sys_now(void) {
    return ticks_ms;
}

/* Random numbers for lwIP (LWIP_RAND in arch/cc.h): xorshift32, seeded
 * from the unique ID so each board gets its own sequence. Fine for DHCP;
 * TCP will want the hardware random number generator. */
uint32_t lwip_rand(void) {
    static uint32_t x;
    if (x == 0) {
        for (int i = 0; i < 12; i++) {
            x = x * 31 + UID[i];
        }
        x |= 1;  /* xorshift must not start at 0 */
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

/* Make a MAC address from the unique ID. The first byte 0x02 marks it
 * "locally administered": outside any vendor's range, so it can't clash
 * with a real one. The 12 ID bytes are folded into the other 5. */
void make_mac(uint8_t *mac) {
    mac[0] = 0x02;
    for (int i = 1; i < 6; i++) {
        mac[i] = 0;
    }
    for (int i = 0; i < 12; i++) {
        mac[1 + i % 5] ^= UID[i];
    }
}

/* Ethernet driver: moves frames between RAM and the MAC using its DMA.
 *
 * The DMA works from two rings of descriptors in RAM, one for receive and
 * one for transmit, each descriptor pointing at one frame buffer. The OWN
 * bit in a descriptor says who may touch it: set = the DMA, clear = the
 * CPU. To send, the CPU fills a buffer and sets OWN; the DMA clears it
 * once the frame is out. To receive, the CPU gives every descriptor to
 * the DMA; the DMA fills a buffer and clears OWN, and the CPU gives it
 * back once the frame is copied out. All of this must be in SRAM, which
 * it is: the DMA can't reach CCM RAM, and the linker script doesn't use
 * it. */

#define ETH_RX_COUNT  4
#define ETH_TX_COUNT  4
#define ETH_BUF_SIZE  1524  /* full frame 1514 + CRC 4, multiple of 4 */

/* Descriptor bits (RM0090 section 33.6.7, normal descriptors) */
#define DESC_OWN      (1u << 31)
#define TDES0_LS      (1u << 29)  /* buffer holds the frame's last byte */
#define TDES0_FS      (1u << 28)  /* ... and its first byte */
#define TDES0_TCH     (1u << 20)  /* word 3 points to the next descriptor */
#define RDES0_ES      (1u << 15)  /* error summary: CRC error, too long... */
#define RDES0_FS      (1u << 9)
#define RDES0_LS      (1u << 8)
#define RDES1_RCH     (1u << 14)

struct eth_desc {
    volatile uint32_t status;  /* OWN, flags, received frame length */
    uint32_t size;             /* buffer size, RCH for receive */
    uint32_t buf;              /* buffer address */
    uint32_t next;             /* next descriptor address */
};

struct eth_desc eth_rx_desc[ETH_RX_COUNT];
struct eth_desc eth_tx_desc[ETH_TX_COUNT];
uint8_t eth_rx_buf[ETH_RX_COUNT][ETH_BUF_SIZE] __attribute__((aligned(4)));
uint8_t eth_tx_buf[ETH_TX_COUNT][ETH_BUF_SIZE] __attribute__((aligned(4)));
uint32_t eth_rx_next;  /* next receive descriptor to check */
uint32_t eth_tx_next;  /* next transmit descriptor to fill */

/* Finish all memory writes (buffers, descriptors) before telling the DMA
 * about them; "memory" also stops the compiler moving writes past it */
#define DSB() __asm volatile ("dsb" ::: "memory")

/* Put a pin in alternate function mode, very high speed */
void gpio_af(uint32_t base, uint32_t pin, uint32_t af) {
    uint32_t afr = base + (pin < 8 ? 0x20 : 0x24);  /* AFRL or AFRH */
    uint32_t shift = (pin % 8) * 4;
    REG(base + 0x00) = (REG(base + 0x00) & ~(3u << pin * 2))  /* MODER */
                     | (2u << pin * 2);
    REG(base + 0x08) |= 3u << pin * 2;                         /* OSPEEDR */
    REG(afr) = (REG(afr) & ~(0xFu << shift)) | (af << shift);
}

/* Set up the MAC and its DMA. RMII mode and the MAC clock are already on
 * from mdio_init(). Returns 0 if the MAC never comes out of reset. */
int eth_init(const uint8_t *mac) {
    RCC_AHB1ENR |= (1u << 1) | (1u << 6)     /* GPIOBEN, GPIOGEN */
                 | (1u << 26) | (1u << 27);  /* ETHMACTXEN, ETHMACRXEN */

    /* RMII pins besides MDC/MDIO, all AF11: PA1 = REF_CLK, PA7 = CRS_DV,
     * PC4 = RXD0, PC5 = RXD1, PG11 = TX_EN, PG13 = TXD0, PB13 = TXD1 */
    gpio_af(GPIOA_BASE, 1, 11);
    gpio_af(GPIOA_BASE, 7, 11);
    gpio_af(GPIOC_BASE, 4, 11);
    gpio_af(GPIOC_BASE, 5, 11);
    gpio_af(GPIOG_BASE, 11, 11);
    gpio_af(GPIOG_BASE, 13, 11);
    gpio_af(GPIOB_BASE, 13, 11);

    /* Software reset of the MAC and DMA. The reset runs on the 50 MHz
     * REF_CLK from the PHY, so it never finishes without that clock. */
    ETH_DMABMR |= 1u << 0;  /* SR */
    uint32_t start = ticks_ms;
    while (ETH_DMABMR & 1u) {
        if (ticks_ms - start > 100) {
            return 0;
        }
    }

    /* Receive filter 0 (the reset default) accepts frames sent to our
     * address plus broadcasts, and drops other unicast and multicast */
    ETH_MACFFR = 0;
    ETH_MACA0HR = (uint32_t)mac[5] << 8 | mac[4];
    ETH_MACA0LR = (uint32_t)mac[3] << 24 | (uint32_t)mac[2] << 16
                | (uint32_t)mac[1] << 8 | mac[0];

    /* Link each ring into a circle and give the receive buffers to the
     * DMA. Transmit descriptors stay with the CPU until there's a frame. */
    for (int i = 0; i < ETH_RX_COUNT; i++) {
        eth_rx_desc[i].size = RDES1_RCH | ETH_BUF_SIZE;
        eth_rx_desc[i].buf = (uint32_t)eth_rx_buf[i];
        eth_rx_desc[i].next = (uint32_t)&eth_rx_desc[(i + 1) % ETH_RX_COUNT];
        eth_rx_desc[i].status = DESC_OWN;
    }
    for (int i = 0; i < ETH_TX_COUNT; i++) {
        eth_tx_desc[i].buf = (uint32_t)eth_tx_buf[i];
        eth_tx_desc[i].next = (uint32_t)&eth_tx_desc[(i + 1) % ETH_TX_COUNT];
        eth_tx_desc[i].status = TDES0_TCH;
    }
    DSB();
    ETH_DMARDLAR = (uint32_t)eth_rx_desc;
    ETH_DMATDLAR = (uint32_t)eth_tx_desc;

    /* Turn on the transmitter and receiver. Speed and duplex are set by
     * eth_set_mode() once the link is up. */
    ETH_MACCR |= (1u << 3) | (1u << 2);  /* TE | RE */

    /* Store and forward (the DMA only moves complete frames between the
     * MAC's FIFOs and RAM), then start both DMA directions */
    ETH_DMAOMR = (1u << 25) | (1u << 21)   /* RSF | TSF */
               | (1u << 13) | (1u << 1);   /* ST | SR */
    return 1;
}

/* Match the MAC to the PHY's negotiated mode, as returned by phy_mode() */
void eth_set_mode(uint32_t mode) {
    uint32_t cr = ETH_MACCR & ~((1u << 14) | (1u << 11));  /* FES | DM */
    if (mode & 2u) {
        cr |= 1u << 14;  /* 100 Mbit/s */
    }
    if (mode & 4u) {
        cr |= 1u << 11;  /* full duplex */
    }
    ETH_MACCR = cr;
}

/* Send one complete Ethernet frame (lwIP's linkoutput). The MAC adds
 * the padding and CRC. */
err_t eth_linkoutput(struct netif *netif, struct pbuf *p) {
    (void)netif;
    if (p->tot_len > ETH_BUF_SIZE) {
        return ERR_BUF;
    }

    /* Every buffer still queued: give the DMA a moment to send one */
    struct eth_desc *d = &eth_tx_desc[eth_tx_next];
    uint32_t start = ticks_ms;
    while (d->status & DESC_OWN) {
        if (ticks_ms - start > 10) {
            return ERR_IF;
        }
    }

    pbuf_copy_partial(p, eth_tx_buf[eth_tx_next], p->tot_len, 0);
    d->size = p->tot_len;
    DSB();
    d->status = DESC_OWN | TDES0_LS | TDES0_FS | TDES0_TCH;
    DSB();
    ETH_DMASR = 1u << 2;  /* clear TBUS, "transmit buffer unavailable" */
    ETH_DMATPDR = 0;      /* poll demand: the DMA looks at the ring again */
    eth_tx_next = (eth_tx_next + 1) % ETH_TX_COUNT;
    return ERR_OK;
}

struct netif eth_netif;

/* Hand every frame the DMA has received to lwIP. Called from the main
 * loop. */
void eth_poll(void) {
    for (;;) {
        struct eth_desc *d = &eth_rx_desc[eth_rx_next];
        uint32_t status = d->status;
        if (status & DESC_OWN) {
            return;  /* still the DMA's: nothing new */
        }

        /* A good frame fits in one buffer, so it's both the first and
         * last segment, without errors. Anything else is dropped. */
        if ((status & (RDES0_ES | RDES0_FS | RDES0_LS))
                == (RDES0_FS | RDES0_LS)) {
            uint32_t len = ((status >> 16) & 0x3FFFu) - 4;  /* minus CRC */
            struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
            if (p) {
                pbuf_take(p, eth_rx_buf[eth_rx_next], (u16_t)len);
                if (eth_netif.input(p, &eth_netif) != ERR_OK) {
                    pbuf_free(p);
                }
            }
        }

        d->status = DESC_OWN;  /* give the buffer back */
        eth_rx_next = (eth_rx_next + 1) % ETH_RX_COUNT;
        ETH_DMASR = 1u << 7;  /* clear RBUS, "receive buffer unavailable" */
        ETH_DMARPDR = 0;      /* restart if it stopped for lack of buffers */
    }
}

/* Called by netif_add() to set up our network interface */
err_t eth_netif_init(struct netif *netif) {
    netif->name[0] = 'e';
    netif->name[1] = '0';
    netif->output = etharp_output;  /* IP packet: find the MAC via ARP */
    netif->linkoutput = eth_linkoutput;  /* complete Ethernet frame */
    netif->mtu = 1500;
    netif->hwaddr_len = 6;
    make_mac(netif->hwaddr);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP
                 | NETIF_FLAG_ETHERNET;
    if (!eth_init(netif->hwaddr)) {
        uart_puts("Ethernet MAC reset timed out, no REF_CLK from PHY?\r\n");
    }
    return ERR_OK;
}

/* Called by lwIP when the IP address changes, e.g. when DHCP gets one */
void net_status(struct netif *netif) {
    uart_puts("IP address ");
    uart_puts(ip4addr_ntoa(netif_ip4_addr(netif)));
    uart_puts("\r\n");
}

/* Bring up lwIP with one Ethernet interface and start DHCP. DHCP waits
 * for netif_set_link_up() before it sends anything. */
void net_init(void) {
    lwip_init();
    netif_add(&eth_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4,
              NULL, eth_netif_init, ethernet_input);
    netif_set_default(&eth_netif);
    netif_set_up(&eth_netif);
    netif_set_status_callback(&eth_netif, net_status);
    dhcp_start(&eth_netif);
    uart_puts("lwIP up, MAC ");
    uart_putmac(eth_netif.hwaddr);
    uart_puts("\r\n");
}

/* The PHY's link went up or down: report it, match the MAC's speed and
 * duplex to the PHY, and tell lwIP */
void link_changed(uint32_t phy, int up) {
    phy_print_link(phy, up);
    if (up) {
        eth_set_mode(phy_mode(phy));
        netif_set_link_up(&eth_netif);
    } else {
        netif_set_link_down(&eth_netif);
    }
}

void adc_init(void) {
    RCC_APB2ENR |= 1u << 8;  /* ADC1EN */

    /* The temperature sensor is ADC1 channel 18, shared with VBAT. Enable
     * the sensor (TSVREFE) and make sure VBAT is off (VBATE): if both are
     * set, the ADC measures VBAT. ADC clock = APB2 / 4 (ADCPRE = 01):
     * 84 MHz / 4 = 21 MHz, under the 36 MHz maximum. */
    ADC_CCR = (ADC_CCR & ~((1u << 22) | (3u << 16))) | (1u << 23) | (1u << 16);

    /* Sensor needs >= 10 us sampling: 480 cycles at 21 MHz = 23 us.
     * Channel 18 sample time is SMPR1 bits 26:24. */
    ADC1_SMPR1 = (ADC1_SMPR1 & ~(7u << 24)) | (7u << 24);

    ADC1_SQR1 = 0;   /* sequence length 1 */
    ADC1_SQR3 = 18;  /* first conversion: channel 18 */
    ADC1_CR2 = 1u << 0;  /* ADON */

    delay_ms(1);  /* ADC and sensor startup (a few us) */
}

/* One conversion, returns the 12-bit result */
uint32_t adc_read(void) {
    ADC1_CR2 |= 1u << 30;  /* SWSTART */
    while (!(ADC1_SR & (1u << 1))) {  /* wait for EOC */
    }
    return ADC1_DR;  /* reading DR clears EOC */
}

/* Convert a raw reading to tenths of a degree C by linear interpolation
 * between the two calibration points */
int32_t temp_tenths(uint32_t raw) {
    int32_t cal1 = TS_CAL1;
    int32_t cal2 = TS_CAL2;
    return 300 + (800 * ((int32_t)raw - cal1)) / (cal2 - cal1);
}

void systick_init(void) {
    SYST_RVR = sysclk_hz / 1000 - 1;  /* 1 ms tick */
    SYST_CVR = 0;
    SYST_CSR = 7;                     /* CLKSOURCE = core | TICKINT | ENABLE */
}

int main(void) {
    int clock_ok = clock_init();
    uart_init();
    systick_init();
    if (!clock_ok) {
        uart_puts("HSE clock missing, running on HSI 16 MHz\r\n");
    }
    adc_init();
    mdio_init();

    int phy = phy_find();
    int link = 0;
    if (phy < 0) {
        uart_puts("no PHY found\r\n");
    } else {
        phy_init((uint32_t)phy);
        uart_puts("PHY at address ");
        uart_putu((uint32_t)phy);
        uart_puts(", ID ");
        uart_puthex16(mdio_read((uint32_t)phy, PHY_ID1));
        uart_putc(':');
        uart_puthex16(mdio_read((uint32_t)phy, PHY_ID2));
        uart_puts("\r\n");
    }

    /* After the PHY reset, so its REF_CLK is steady for the MAC reset */
    net_init();
    if (phy >= 0) {
        link = phy_link_up((uint32_t)phy);
        link_changed((uint32_t)phy, link);
    }

    /* lwIP needs sys_check_timeouts() called often (DHCP, ARP timers), so
     * the loop no longer sleeps: it spins, and does the once-per-second
     * work whenever a second has passed. */
    uint32_t last = ticks_ms - 1000;
    for (;;) {
        eth_poll();
        sys_check_timeouts();
        if (ticks_ms - last < 1000) {
            continue;
        }
        last += 1000;

        if (phy >= 0) {
            int now = phy_link_up((uint32_t)phy);
            if (now != link) {
                link = now;
                link_changed((uint32_t)phy, link);
            }
        }

        int32_t t = temp_tenths(adc_read());
        uart_puts("temp ");
        if (t < 0) {
            uart_putc('-');
            t = -t;
        }
        uart_putu((uint32_t)t / 10);
        uart_putc('.');
        uart_putu((uint32_t)t % 10);
        uart_puts(" C\r\n");
    }
}
