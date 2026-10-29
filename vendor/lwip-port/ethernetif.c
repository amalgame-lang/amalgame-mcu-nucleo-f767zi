/* STM32F767 Ethernet (RMII, LAN8742 PHY) lwIP netif via libopencm3's polled MAC.
 *
 * The Nucleo-F767ZI wires the on-chip ETH MAC in RMII to a LAN8742 PHY at address
 * 0. libopencm3 manages the DMA descriptor ring internally and exposes a simple
 * polled eth_tx()/eth_rx(), which fits the NO_SYS / loop{}-polled model.
 * No DMA interrupts at the ethernetif level.
 *
 * NOTE: link-proof correctness only requires these to compile + resolve. Bringing
 * the link actually up at runtime needs the PLL/ETH clocks configured at startup
 * (blink runs on 16 MHz HSI with no PLL).
 */
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#if LWIP_IPV6
#include "lwip/ethip6.h"
#endif
#include "netif/ethernet.h"
#include "ethernetif.h"
#include "net_mcu.h"          /* net_millis() for the link-up wait */

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/ethernet/mac.h>
#include <libopencm3/ethernet/mac_stm32fxx7.h>
#include <libopencm3/ethernet/phy.h>

/* Options de carte (README, « Points d'accroche pour une autre carte ») : un en-tête fourni par l'application,
 * -DBOARD_EXT_HEADER='"ma_carte.h"' (+ -I vers son dossier), peut redéfinir les broches ci-dessous. Sans lui : Nucleo-F767ZI. */
#ifdef BOARD_EXT_HEADER
#include BOARD_EXT_HEADER
#endif
/* Broches RMII d'émission (TX_EN, TXD0, TXD1) : le F767 les propose sur le port B ou sur le port G (AF11, DS11532).
 * ETH_RMII_TXB_PINS = celles du port B, ETH_RMII_TXG_PINS = celles du port G (0 = aucune). */
#ifndef ETH_RMII_TXB_PINS
#define ETH_RMII_TXB_PINS GPIO13               /* Nucleo-F767ZI : TXD1 */
#endif
#ifndef ETH_RMII_TXG_PINS
#define ETH_RMII_TXG_PINS (GPIO11 | GPIO13)    /* Nucleo-F767ZI : TX_EN, TXD0 */
#endif

/* SYSCFG_PMC bit 23 = MII/RMII select (named on F1 only in libopencm3). */
#ifndef SYSCFG_PMC_MII_RMII_SEL
#define SYSCFG_PMC_MII_RMII_SEL (1 << 23)
#endif

#define ETH_PHY       0                 /* adresse attendue (PHYAD0 tiré à GND) ; la vraie est cherchée au démarrage */
static uint8_t mcnet_phy = ETH_PHY;
volatile int ethernetif_phy_addr = -1;   /* adresse trouvée, -1 = aucun PHY ne répond (diagnostic) */
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
 *   PB13 TXD1, PG11 TX_EN, PG13 TXD0.
 * Les broches d'émission suivent ETH_RMII_TXB_PINS / ETH_RMII_TXG_PINS (options de carte, voir plus haut). */
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

#if ETH_RMII_TXB_PINS
    gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, ETH_RMII_TXB_PINS);
    gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, ETH_RMII_TXB_PINS);
    gpio_set_af(GPIOB, GPIO_AF11, ETH_RMII_TXB_PINS);
#endif

    gpio_mode_setup(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO1 | GPIO4 | GPIO5);
    gpio_set_output_options(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO1 | GPIO4 | GPIO5);
    gpio_set_af(GPIOC, GPIO_AF11, GPIO1 | GPIO4 | GPIO5);

#if ETH_RMII_TXG_PINS
    gpio_mode_setup(GPIOG, GPIO_MODE_AF, GPIO_PUPD_NONE, ETH_RMII_TXG_PINS);
    gpio_set_output_options(GPIOG, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, ETH_RMII_TXG_PINS);
    gpio_set_af(GPIOG, GPIO_AF11, ETH_RMII_TXG_PINS);
#endif
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

/* ---- PHY (LAN8742A) explicit configuration + link monitor ----------------------
 * Measured 2026-09-03 (openocd, board stuck after an st-flash reset, 3rd time): BMCR
 * read back 0x0000 = auto-negotiation DISABLED, forced 10 Mbit/s half-duplex, link
 * down — while the MAC is hard-coded 100 Mbit/s full-duplex (libopencm3 eth_init).
 * The two never talk: 0 frames received, DHCP never completes. Writing BMCR=0x1200
 * by MDIO brought the link up at 100/FD within seconds. Cause = the PHY's MODE
 * straps sampled wrong at that hardware reset (nRST shared with the MCU). So: never
 * trust the straps — advertise 10/100 HD/FD, enable+restart AN, wait for the link,
 * then set the MAC to what was actually negotiated; and keep watching the link. */
#define PHY_BMCR    0
#define PHY_BMSR    1
#define PHY_ANAR    4
#define PHY_SCSR    31                   /* LAN87xx special control/status */
#define PHY_BMCR_AN_ENABLE  (1 << 12)
#define PHY_BMCR_AN_RESTART (1 << 9)
#define PHY_BMSR_LINK       (1 << 2)
#define PHY_ANAR_ALL        0x01E1       /* 10HD 10FD 100HD 100FD + IEEE 802.3 selector */

static int  mcnet_link_state = 0;        /* cached: 1 = up */
static int  mcnet_link_speed = 0;        /* 10 / 100 */
static int  mcnet_link_fdx   = 0;
static uint32_t mcnet_link_next_ms = 0;
static struct netif *mcnet_link_netif = NULL;

static void phy_apply_negotiated(void)
{
    uint16_t scsr = eth_smi_read(mcnet_phy, PHY_SCSR);
    int spd = (scsr >> 2) & 0x7;          /* 001=10HD 101=10FD 010=100HD 110=100FD */
    mcnet_link_speed = (spd & 0x2) ? 100 : 10;
    mcnet_link_fdx   = (spd & 0x4) ? 1 : 0;
    uint32_t cr = ETH_MACCR;
    if (mcnet_link_speed == 100) cr |= ETH_MACCR_FES; else cr &= ~ETH_MACCR_FES;
    if (mcnet_link_fdx)          cr |= ETH_MACCR_DM;  else cr &= ~ETH_MACCR_DM;
    ETH_MACCR = cr;
}

/* nRST du PHY piloté par une broche du MCU (option de carte : ETH_PHY_NRST_PORT, ETH_PHY_NRST_PIN, ETH_PHY_NRST_RCC).
 * Le LAN8742A doit rester en reset ≥ 25 ms après que ses alimentations sont stables pour figer correctement ses
 * broches de configuration (PHYAD0, nINTSEL, REGOFF, MODE ; DS00001989A tableau 5-11) : un nRST relié au NRST du MCU
 * ne lui en donne que 2 à 4. Sans ces macros, rien (Nucleo-F767ZI : nRST du PHY sur NRST, SB17). */
static void phy_reset_materiel(void)
{
#ifdef ETH_PHY_NRST_PORT
    rcc_periph_clock_enable(ETH_PHY_NRST_RCC);
    gpio_clear(ETH_PHY_NRST_PORT, ETH_PHY_NRST_PIN);             /* niveau bas AVANT le passage en sortie : pas de pointe */
    gpio_mode_setup(ETH_PHY_NRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, ETH_PHY_NRST_PIN);
    gpio_set_output_options(ETH_PHY_NRST_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, ETH_PHY_NRST_PIN);
    uint32_t t0 = net_millis();
    while (net_millis() - t0 < 30u) { }                          /* ≥ 25 ms de reset, alimentations stables depuis longtemps */
    gpio_set(ETH_PHY_NRST_PORT, ETH_PHY_NRST_PIN);
    t0 = net_millis();
    while (net_millis() - t0 < 10u) { }                          /* configuration figée, PHY prêt pour le MDIO */
#endif
}

/* Reset logiciel borné : 500 ms au plus (le reset de libopencm3 boucle sans fin si le PHY ne répond pas). */
static int phy_reset_borne(uint8_t phy)
{
    eth_smi_write(phy, PHY_REG_BCR, PHY_REG_BCR_RESET);
    uint32_t t0 = net_millis();
    while (net_millis() - t0 < 500u) {
        uint16_t v = eth_smi_read(phy, PHY_REG_BCR);
        if (v != 0xFFFF && !(v & PHY_REG_BCR_RESET)) return 0;
    }
    return -1;
}

/* Trouve le PHY : l'adresse attendue d'abord, sinon les 32 adresses (identifiant LAN87xx = 0x0007 en registre 2).
 * Une configuration mal figée au reset donne une autre adresse : on la trouve et on la note (diagnostic). */
static void phy_trouver_et_reset(void)
{
    for (int k = -1; k < 32; k++) {
        uint8_t a = (k < 0) ? ETH_PHY : (uint8_t) k;
        if (k == ETH_PHY) continue;
        if (eth_smi_read(a, 2) == 0x0007 && phy_reset_borne(a) == 0) {
            mcnet_phy = a; ethernetif_phy_addr = a;
            return;
        }
    }
    ethernetif_phy_addr = -1;                                     /* aucun PHY : la carte démarre sans réseau */
}

static void phy_configure(void)
{
    eth_smi_write(mcnet_phy, PHY_ANAR, PHY_ANAR_ALL);
    eth_smi_write(mcnet_phy, PHY_BMCR, PHY_BMCR_AN_ENABLE | PHY_BMCR_AN_RESTART);
    uint32_t t0 = net_millis();
    while (net_millis() - t0 < 3000u) {   /* typical AN completes in ~1.5 s */
        if (eth_smi_read(mcnet_phy, PHY_BMSR) & PHY_BMSR_LINK) break;
    }
}

/* Poll the link every 250 ms; tell lwIP about transitions (it re-runs DHCP itself). */
void ethernetif_link_poll(struct netif *netif)
{
    uint32_t now = net_millis();
    if ((int32_t)(now - mcnet_link_next_ms) < 0) return;
    mcnet_link_next_ms = now + 250u;
    /* Aucun PHY trouvé au démarrage (alimentation tardive, adresse non nulle qui répond en retard) : on le recherche
     * toutes les 5 s, sans attente — identifiant puis négociation relancée, le lien sera vu au passage suivant (2026-10-26). */
    if (ethernetif_phy_addr < 0) {
        static uint32_t next_scan;
        if ((int32_t)(now - next_scan) < 0) return;
        next_scan = now + 5000u;
        for (int k = -1; k < 32; k++) {
            uint8_t a = (k < 0) ? ETH_PHY : (uint8_t) k;
            if (k == ETH_PHY) continue;
            if (eth_smi_read(a, 2) == 0x0007) {
                mcnet_phy = a; ethernetif_phy_addr = a;
                eth_smi_write(mcnet_phy, PHY_ANAR, PHY_ANAR_ALL);
                eth_smi_write(mcnet_phy, PHY_BMCR, PHY_BMCR_AN_ENABLE | PHY_BMCR_AN_RESTART);
                break;
            }
        }
        return;
    }
    int up = (eth_smi_read(mcnet_phy, PHY_BMSR) & PHY_BMSR_LINK) ? 1 : 0;
    if (up == mcnet_link_state) return;
    mcnet_link_state = up;
    if (up) {
        phy_apply_negotiated();
        netif_set_link_up(netif);
    } else {
        netif_set_link_down(netif);
    }
}

int ethernetif_link_up(void)    { return mcnet_link_state; }
int ethernetif_link_speed(void) { return mcnet_link_state ? mcnet_link_speed * (mcnet_link_fdx ? 1 : -1) : 0; }

err_t ethernetif_init(struct netif *netif)
{
    mcnet_link_netif = netif;
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
    /* NETIF_FLAG_IGMP : sans lui, igmp_joingroup_netif échoue et le répondeur mDNS n'entend jamais le
     * groupe 224.0.0.251 — la carte ne répond pas à son nom. Même piège que NETIF_FLAG_MLD6 en IPv6. */
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;   /* link state set by the monitor */
#if LWIP_IGMP
    netif->flags |= NETIF_FLAG_IGMP;
#endif
#if LWIP_IPV6
    /* Sans ces deux lignes, rien ne SORT en IPv6 : pas de sollicitation de routeur, donc pas de RA,
     * donc pas d'adresse globale — l'interface reste sur sa seule lien-local. NETIF_FLAG_MLD6 permet
     * à lwIP de s'inscrire aux groupes multicast (all-nodes, solicited-node). */
    netif->output_ip6 = ethip6_output;
    netif->flags |= NETIF_FLAG_MLD6;
#endif

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
    /* eth_init() de libopencm3 appelle phy_reset(), qui boucle SANS FIN si le PHY ne répond pas (MDIO lit 0xFFFF) :
     * on refait ici ses réglages MAC, avec un reset du PHY borné dans le temps (relecture du 2026-10-26). */
    ETH_MACMIIAR = ETH_CLK_150_168MHZ;
    phy_reset_materiel();
    phy_trouver_et_reset();
    ETH_MACCR = ETH_MACCR_CSTF | ETH_MACCR_FES | ETH_MACCR_DM | ETH_MACCR_APCS | ETH_MACCR_RD;
    ETH_MACFFR = ETH_MACFFR_RA | ETH_MACFFR_PM;
    ETH_MACHTHR = 0;
    ETH_MACHTLR = 0;
    ETH_MACFCR = (0x100 << ETH_MACFCR_PT_SHIFT);
    ETH_MACVLANTR = 0;
    ETH_DMAOMR = ETH_DMAOMR_DTCEFD | ETH_DMAOMR_RSF | ETH_DMAOMR_DFRF | ETH_DMAOMR_TSF | ETH_DMAOMR_FEF | ETH_DMAOMR_OSF;
    ETH_DMABMR = ETH_DMABMR_AAB | ETH_DMABMR_FB | (32 << ETH_DMABMR_RDP_SHIFT) | (32 << ETH_DMABMR_PBL_SHIFT) |
                 ETH_DMABMR_PM_2_1 | ETH_DMABMR_USP;
    eth_set_mac(mac_addr);
    eth_desc_init(eth_desc_buffer, ETH_TXBUFNB, ETH_RXBUFNB, ETH_BUF_SZ, ETH_BUF_SZ, false);
    if (ethernetif_phy_addr >= 0) phy_configure();   /* sans PHY : pas d'attente de lien de 3 s, la carte continue sans réseau */
    eth_start();
    mcnet_link_next_ms = net_millis();   /* first monitor pass right away (sets link up if AN done) */
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
