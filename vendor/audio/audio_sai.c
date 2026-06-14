/* STM32F767 SAI1 audio I/O for MusiCall-Box — the actual board (from the original
 * firmware's lib/AudioOutput + lib/AudioInput):
 *   DAC = TI PCM5102  on SAI1 Block A (MASTER TX),  DMA2 Stream1 Ch0
 *   ADC = TI PCM1802  on SAI1 Block B (SLAVE  RX, synchronous to A)
 * Both are plain I2S chips configured by PINS — NO I2C control bus — so there is no
 * codec register sequence to run (simpler than WM8960/SGTL5000).
 *
 * Pins (GPIOE, AF6 = SAI1):
 *   PE2 = MCLK (12.288 MHz = 48 kHz x 256, needs PLLSAI), PE5 = BCLK, PE4 = FS/WS,
 *   PE6 = SD_A (block A -> DAC). Block B (RX <- ADC) is synchronous (shares BCLK/FS)
 *   and needs its OWN data line SD_B = PE3 (AF6). NOTE: the original wired block B's
 *   GPIO to PE6 too — that collides with block A's TX data pin; SD_B must be separate
 *   (a real bug in the original; PE3 used here).
 *
 * Mono, 16-bit slots, frame 32 / active 16. DMA circular double buffer; the half/full
 * transfer-complete ISRs refill (TX) / capture (RX) a 120-sample frame — the 2.5 ms
 * cadence is the DMA half-buffer, no thread/timer.
 *
 * libopencm3 has no SAI driver for F7, so SAI registers are accessed via MMIO; DMA +
 * GPIO use libopencm3. SCAFFOLD: link-proves and matches the board; exact SAI register
 * bits + PLLSAI clock still need validation on hardware before sound comes out.
 */
#include "audio_sai.h"
#include <libopencm3/cm3/common.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/dma.h>

/* SAI1 on APB2 (SAI1_BASE comes from libopencm3 memorymap). Block A registers at
 * +0x04.., block B at +0x24.. (RM0410). */
#define SAI_ACR1   MMIO32(SAI1_BASE + 0x04)
#define SAI_ACR2   MMIO32(SAI1_BASE + 0x08)
#define SAI_AFRCR  MMIO32(SAI1_BASE + 0x0C)
#define SAI_ASLOTR MMIO32(SAI1_BASE + 0x10)
#define SAI_ASR    MMIO32(SAI1_BASE + 0x18)
#define SAI_ADR    MMIO32(SAI1_BASE + 0x20)
#define SAI_BCR1   MMIO32(SAI1_BASE + 0x24)
#define SAI_BCR2   MMIO32(SAI1_BASE + 0x28)
#define SAI_BFRCR  MMIO32(SAI1_BASE + 0x2C)
#define SAI_BSLOTR MMIO32(SAI1_BASE + 0x30)
#define SAI_BDR    MMIO32(SAI1_BASE + 0x40)
#define SAI_xCR1_SAIEN  (1u << 16)
#define SAI_xCR1_DMAEN  (1u << 17)

#define AUD_RING 4

/* playback: loop producer / TX-DMA-ISR consumer */
static int16_t          tx_ring[AUD_RING][AUD_FRAME];
static volatile int     tx_head, tx_tail;
static int16_t          tx_dma[2 * AUD_FRAME];

/* capture: RX-DMA-ISR producer / loop consumer */
static int16_t          rx_dma[2 * AUD_FRAME];
static int16_t          rx_frame[AUD_FRAME];
static volatile int     rx_ready;

static void dma_setup(uint8_t stream, uint32_t periph, void *mem, uint32_t dir)
{
    dma_stream_reset(DMA2, stream);
    dma_channel_select(DMA2, stream, DMA_SxCR_CHSEL_0);
    dma_set_peripheral_address(DMA2, stream, periph);
    dma_set_memory_address(DMA2, stream, (uint32_t) mem);
    dma_set_number_of_data(DMA2, stream, 2 * AUD_FRAME);
    dma_set_transfer_mode(DMA2, stream, dir);
    dma_set_peripheral_size(DMA2, stream, DMA_SxCR_PSIZE_16BIT);
    dma_set_memory_size(DMA2, stream, DMA_SxCR_MSIZE_16BIT);
    dma_enable_memory_increment_mode(DMA2, stream);
    dma_enable_circular_mode(DMA2, stream);
    dma_enable_half_transfer_interrupt(DMA2, stream);
    dma_enable_transfer_complete_interrupt(DMA2, stream);
    dma_enable_stream(DMA2, stream);
}

/* SAI1 pins on GPIOE, AF6. Block A: PE2 MCLK, PE4 FS, PE5 BCLK, PE6 SD_A. */
static void sai_gpio_block_a(void)
{
    rcc_periph_clock_enable(RCC_GPIOE);
    uint16_t pins = GPIO2 | GPIO4 | GPIO5 | GPIO6;
    gpio_mode_setup(GPIOE, GPIO_MODE_AF, GPIO_PUPD_NONE, pins);
    gpio_set_output_options(GPIOE, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, pins);
    gpio_set_af(GPIOE, GPIO_AF6, pins);
}

/* Block B is synchronous (shares A's BCLK/FS) — only its own data line SD_B = PE3. */
static void sai_gpio_block_b(void)
{
    rcc_periph_clock_enable(RCC_GPIOE);
    gpio_mode_setup(GPIOE, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO3);
    gpio_set_output_options(GPIOE, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO3);
    gpio_set_af(GPIOE, GPIO_AF6, GPIO3);
}

void aud_spk_start(void)
{
    rcc_periph_clock_enable(RCC_SAI1EN);
    rcc_periph_clock_enable(RCC_DMA2);
    sai_gpio_block_a();

    /* SAI block A = master TX, free protocol, 16-bit, mono, MCKDIV=2 — mirrors the
     * original mbed HAL config (AudioOutput::initSAI). The exact MCKDIV / PLLSAI must
     * be trimmed for 48 kHz, but the PCM5102A (SCK=GND, internal PLL) tolerates an
     * approximate rate, so this is enough to get sound out for bring-up.
     *   ACR1  : MODE=00(MasterTX) PRTCFG=00(free) DS=100(16-bit) MONO=1 OUTDRIV=1
     *           NODIV=0 + MCKDIV=2: master divider ON → MCLK = kernel(49.152)/(2*2) =
     *           12.288MHz (256*48k), SCK = MCLK/8 = 1.536MHz, LRCK = 48kHz. (NODIV=1
     *           was the bug: it bypasses the divider so SCK=kernel=49MHz → 32x too fast,
     *           no 48kHz LRCK → DAC silent. Matches the original mbed NoDivider=ENABLE.)
     *   ACR2  : FTH=001 (1/4 FIFO)
     *   AFRCR : FRL=31(frame 32) FSALL=15(active 16) FSDEF=1 FSPOL=0(low) FSOFF=1 (I2S)
     *   ASLOTR: SLOTSZ=01(16-bit) NBSLOT=1(=2 slots) SLOTEN=slots 0+1 (0x3)
     *           MONO mode (ACR1) is only valid with 2 slots — it duplicates slot0
     *           onto slot1, so the DMA still feeds one sample per frame. With 1 slot
     *           the FS framed at the bit-clock rate (no 48 kHz LRCK → DAC silent). */
    SAI_ACR1   = 0x00203080u;   /* NODIV=0, MCKDIV=2 (bits20-23), MONO=1 */
    SAI_ACR2   = 0x00000001u;
    /* 64-fS BCK frame (64 BCK / LRCK = 3.072 MHz). The PCM5102 DAC tolerates 32-fS, BUT
     * the PCM1802 ADC slave REQUIRES 64-fS or 48-fS BCK (datasheet §7.4.4): at 32-fS only
     * 16 BCK/channel, too few for its 24-bit word -> DOUT stuck. 256-fS MCLK / 64-fS = 4
     * (clean). Both SAI blocks share this frame.
     *   AFRCR : FRL=63(64-bit frame) FSALL=31(active 32) FSDEF=1 FSPOL=0 FSOFF=1 (I2S)
     *   ASLOTR: SLOTSZ=10(32-bit slot) NBSLOT=1(=2 slots) SLOTEN=slots 0+1 (16-bit data
     *           MSB-aligned in each 32-bit slot, FBOFF=0). */
    SAI_AFRCR  = 0x00031F3Fu;   /* 64-bit frame, FS active 32, FSOFF=0 FSPOL=1 = LEFT-JUSTIFIED
                                 * (CJMCU-1802 with all 6 control pins to GND = FMT0/1=Low =
                                 * left-justified 24-bit; match it, not I2S). */
    SAI_ASLOTR = 0x00030180u;   /* 32-bit slots, 2 slots, slots 0+1 enabled */

    dma_setup(DMA_STREAM1, (uint32_t) &SAI_ADR, tx_dma, DMA_SxCR_DIR_MEM_TO_PERIPHERAL);
    nvic_enable_irq(NVIC_DMA2_STREAM1_IRQ);   /* without this the refill ISR never fires → tx_dma stays 0 → silence */

    SAI_ACR1 |= SAI_xCR1_DMAEN;
    SAI_ACR1 |= SAI_xCR1_SAIEN;
}

static void tx_refill(int half)
{
    int16_t *dst = &tx_dma[half * AUD_FRAME];
    if (tx_tail != tx_head) {
        for (int i = 0; i < AUD_FRAME; i++) dst[i] = tx_ring[tx_tail][i];
        tx_tail = (tx_tail + 1) % AUD_RING;
    } else {
        for (int i = 0; i < AUD_FRAME; i++) dst[i] = 0;   /* underrun -> silence */
    }
}

void dma2_stream1_isr(void)
{
    if (dma_get_interrupt_flag(DMA2, DMA_STREAM1, DMA_HTIF)) {
        dma_clear_interrupt_flags(DMA2, DMA_STREAM1, DMA_HTIF);
        tx_refill(0);
    }
    if (dma_get_interrupt_flag(DMA2, DMA_STREAM1, DMA_TCIF)) {
        dma_clear_interrupt_flags(DMA2, DMA_STREAM1, DMA_TCIF);
        tx_refill(1);
    }
}

void aud_spk_push_raw(const int16_t *frame)
{
    int next = (tx_head + 1) % AUD_RING;
    /* Block while the ring is full: the DMA-complete ISR drains it at exactly Fs, so
     * this paces the producer to the hardware rate — ring stays full, no underrun
     * (software DelayMs pacing was unreliable → silence). */
    while (next == tx_tail) { }
    for (int i = 0; i < AUD_FRAME; i++) tx_ring[tx_head][i] = frame[i];
    tx_head = next;
}

void aud_mic_start(void)
{
    rcc_periph_clock_enable(RCC_SAI1EN);
    rcc_periph_clock_enable(RCC_DMA2);
    sai_gpio_block_b();

    /* SAI block B = SLAVE RX, synchronous to block A (which masters BCK/FS/MCLK for
     * the whole bus), free protocol, 16-bit, mono. No clock generation here (it rides
     * block A's clock) — so no NODIV/MCKDIV. Same frame/slot layout as block A.
     *   BCR1  : MODE=11(SlaveRX) PRTCFG=00 DS=100(16-bit) SYNCEN=01(sync w/ block A) MONO=1
     *   BFRCR : FRL=63 FSALL=31 FSDEF=1 FSPOL=0 FSOFF=1 (64-fS frame, matches block A)
     *   BSLOTR: SLOTSZ=10(32-bit) NBSLOT=1(=2 slots) SLOTEN=slots 0+1 (matches block A) */
    SAI_BCR1   = 0x00001483u;   /* slave RX, 16-bit, mono, synchronous */
    SAI_BFRCR  = 0x00031F3Fu;   /* 64-fS frame, FSOFF=0 FSPOL=1 = LEFT-JUSTIFIED (match block A) */
    SAI_BSLOTR = 0x00030180u;   /* 32-bit slots, 2 slots, slots 0+1 enabled */

    dma_setup(DMA_STREAM5, (uint32_t) &SAI_BDR, rx_dma, DMA_SxCR_DIR_PERIPHERAL_TO_MEM);
    nvic_enable_irq(NVIC_DMA2_STREAM5_IRQ);

    SAI_BCR1 |= SAI_xCR1_DMAEN;
    SAI_BCR1 |= SAI_xCR1_SAIEN;
}

static void rx_capture(int half)
{
    for (int i = 0; i < AUD_FRAME; i++) rx_frame[i] = rx_dma[half * AUD_FRAME + i];
    rx_ready = 1;
}

void dma2_stream5_isr(void)
{
    if (dma_get_interrupt_flag(DMA2, DMA_STREAM5, DMA_HTIF)) {
        dma_clear_interrupt_flags(DMA2, DMA_STREAM5, DMA_HTIF);
        rx_capture(0);
    }
    if (dma_get_interrupt_flag(DMA2, DMA_STREAM5, DMA_TCIF)) {
        dma_clear_interrupt_flags(DMA2, DMA_STREAM5, DMA_TCIF);
        rx_capture(1);
    }
}

int aud_mic_ready(void) { return rx_ready; }

void aud_mic_take_raw(int16_t *out)
{
    rx_ready = 0;
    for (int i = 0; i < AUD_FRAME; i++) out[i] = rx_frame[i];
}
