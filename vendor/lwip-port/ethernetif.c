/* STM32F767 Ethernet (RMII, LAN8742 PHY) lwIP netif via libopencm3's polled MAC.
 *
 * The Nucleo-F767ZI wires the on-chip ETH MAC in RMII to a LAN8742 PHY at address
 * 0. libopencm3 manages the DMA descriptor ring internally and exposes a simple
 * polled eth_tx()/eth_rx(), which fits the NO_SYS / loop{}-polled model (see
 * docs/net-mcu.md). No DMA interrupts at the ethernetif level.
 *
 * NOTE: link-proof correctness only requires these to compile + resolve. Bringing
 * the link actually up at runtime needs the PLL/ETH clocks configured at startup
 * (blink runs on 16 MHz HSI with no PLL).
 */
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "ethernetif.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/ethernet/mac.h>
#include <libopencm3/ethernet/mac_stm32fxx7.h>
#include <libopencm3/ethernet/phy.h>

/* SYSCFG_PMC bit 23 = MII/RMII select (named on F1 only in libopencm3). */
#ifndef SYSCFG_PMC_MII_RMII_SEL
#define SYSCFG_PMC_MII_RMII_SEL (1 << 23)
#endif

#define ETH_PHY       0
#define ETH_TXBUFNB   4   /* was 3; +1 slack now that eth_tx's silent refusal is counted (mcnet_tx_drops) */
#define ETH_RXBUFNB   12  /* was 4: a LAN broadcast burst of >4 frames within one 2.5 ms poll overflowed the MAC RX FIFO (ETH_DMAMFBOCR.MFA, measured 2026-09-03) and dropped RTP frames with it */
#define ETH_BUF_SZ    1536

/* eth_desc_init lays out, per buffer, a descriptor (ETH_DES_STD_SIZE=16B) + the data
 * buffer — so the backing store must be nTx*(cTx+16) + nRx*(cRx+16). Size generously
 * (+32/buffer) and 4-byte aligned. Lives in SRAM via the linker (DMA-reachable). */
static uint8_t eth_desc_buffer[(ETH_TXBUFNB + ETH_RXBUFNB) * (ETH_BUF_SZ + 32)]
    __attribute__((aligned(4)));

/* Locally-administered MAC (bit 1 of first octet set, multicast bit clear). The
 * last 3 octets are overwritten at init from the STM32's 96-bit factory unique ID
 * (mac_addr_init) — this was a FIXED value, identical on every board running this
 * firmware, which works fine with exactly one board on a network but silently
 * collides (same MAC on two ports -> DHCP/ARP chaos, only one board ever gets an
 * IP) as soon as two boards join the same LAN. */
static uint8_t mac_addr[6] = { 0x02, 0x00, 0x00, 0x4D, 0x42, 0x01 };

/* STM32F7 96-bit unique device ID register (RM0410 §41.1). */
#define STM32_UID_BASE 0x1FF0F420u

static void mac_addr_init(void)
{
    const volatile uint32_t *uid = (const volatile uint32_t *) STM32_UID_BASE;
    uint32_t mix = uid[0] ^ uid[1] ^ uid[2];
    mac_addr[3] = (uint8_t) (mix >> 16);
    mac_addr[4] = (uint8_t) (mix >> 8);
    mac_addr[5] = (uint8_t) mix;
}

/* Nucleo-F767ZI RMII pinout, all AF11:
 *   PA1 REF_CLK, PA2 MDIO, PA7 CRS_DV, PC1 MDC, PC4 RXD0, PC5 RXD1,
 *   PB13 TXD1, PG11 TX_EN, PG13 TXD0. */
static void rmii_gpio_init(void)
{
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_GPIOG);
    rcc_periph_clock_enable(RCC_SYSCFG);

    /* Select RMII before enabling the MAC clocks. */
    SYSCFG_PMC |= SYSCFG_PMC_MII_RMII_SEL;

    gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO1 | GPIO2 | GPIO7);
    gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO1 | GPIO2 | GPIO7);
    gpio_set_af(GPIOA, GPIO_AF11, GPIO1 | GPIO2 | GPIO7);

    gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO13);
    gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO13);
    gpio_set_af(GPIOB, GPIO_AF11, GPIO13);

    gpio_mode_setup(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO1 | GPIO4 | GPIO5);
    gpio_set_output_options(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO1 | GPIO4 | GPIO5);
    gpio_set_af(GPIOC, GPIO_AF11, GPIO1 | GPIO4 | GPIO5);

    gpio_mode_setup(GPIOG, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO13);
    gpio_set_output_options(GPIOG, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO11 | GPIO13);
    gpio_set_af(GPIOG, GPIO_AF11, GPIO11 | GPIO13);
}

/* Diagnostic: frames refused by eth_tx (descriptor ring full) — silent loss otherwise. */
volatile uint32_t mcnet_tx_drops = 0;

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    static uint8_t txbuf[ETH_BUF_SZ];
    (void) netif;
    u16_t len = pbuf_copy_partial(p, txbuf, sizeof(txbuf), 0);
    if (!eth_tx(txbuf, len)) {          /* TX descriptor still owned by the DMA */
        mcnet_tx_drops++;
        return ERR_IF;
    }
    /* UNCONDITIONAL transmit poll demand. libopencm3's eth_tx only kicks the DMA when
     * it sees TBUS ("buffer unavailable") already set; measured 2026-09-03 with
     * app/full_p2p_drift.am: at a steady 400 frames/s, 1 frame out of ETH_TXBUFNB
     * missed that window and sat in its descriptor until the NEXT eth_tx (2.5 ms
     * later) issued the demand -> frames left the board in back-to-back pairs with
     * a periodic one-frame delay (period == ETH_TXBUFNB: 3 with 3 descriptors, 5
     * with 5). A poll demand while the DMA is already running is a no-op, so
     * always issuing it is safe (ChibiOS does the same). */
    ETH_DMASR = ETH_DMASR_TBUS;
    ETH_DMATPDR = 0;
    return ERR_OK;
}

static struct pbuf *low_level_input(void)
{
    static uint8_t rxbuf[ETH_BUF_SZ];
    uint32_t len = 0;
    if (!eth_rx(rxbuf, &len, sizeof(rxbuf)) || len == 0) {
        return NULL;
    }
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t) len, PBUF_POOL);
    if (p) {
        pbuf_take(p, rxbuf, (u16_t) len);
    }
    return p;
}

err_t ethernetif_init(struct netif *netif)
{
    mac_addr_init();
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->output = etharp_output;
    netif->linkoutput = low_level_output;
    netif->hwaddr_len = 6;
    for (int i = 0; i < 6; i++) {
        netif->hwaddr[i] = mac_addr[i];
    }
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    /* RMII select + GPIO MUST happen BEFORE the MAC clocks are enabled — the
     * MII/RMII choice (SYSCFG_PMC) is sampled as the MAC comes out of reset; set it
     * after and the MAC stays in MII mode with RMII pins → RX never clocks (rx=0). */
    rmii_gpio_init();
    rcc_periph_clock_enable(RCC_ETHMAC);
    rcc_periph_clock_enable(RCC_ETHMACTX);
    rcc_periph_clock_enable(RCC_ETHMACRX);

    /* MDIO (MDC) clock divider must match the REAL HCLK. At 216 MHz, /102 gives
     * MDC = 2.1 MHz (under the PHY's 2.5 MHz max); a smaller divider overclocks MDC
     * and the PHY never answers -> eth_smi_read() spins forever. */
    eth_init(ETH_PHY, ETH_CLK_150_168MHZ);
    eth_set_mac(mac_addr);
    eth_desc_init(eth_desc_buffer, ETH_TXBUFNB, ETH_RXBUFNB, ETH_BUF_SZ, ETH_BUF_SZ, false);
    phy_reset(ETH_PHY);
    eth_start();
    return ERR_OK;
}

/* Bring-up diagnostic: total frames pulled from the MAC (0 ⇒ RX data path dead). */
volatile uint32_t mcnet_rx_frames = 0;

void ethernetif_poll(struct netif *netif)
{
    struct pbuf *p;
    while ((p = low_level_input()) != NULL) {
        mcnet_rx_frames++;
        if (netif->input(p, netif) != ERR_OK) {
            pbuf_free(p);
        }
    }
}
