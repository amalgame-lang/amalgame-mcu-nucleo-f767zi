/* On-chip analog audio for MusiCall-Box (no external codec): the F767's 12-bit DAC
 * drives the speaker (DAC ch1, TIM6-triggered, DMA circular double buffer) and the
 * 12-bit ADC captures the mic (ADC1, DMA circular). Implements the SAME aud_* API as
 * audio_sai.c (audio_sai.h) — link EITHER this OR audio_sai.c; the AM facade
 * (audio_sai.am) and the firmware are identical. This is the fastest path to real
 * sound: a speaker amp on the DAC pin, a mic preamp on an ADC pin, nothing else.
 *
 * SCAFFOLD: link-proves and is the right structure; the timer rate / ADC sample
 * timing / pin routing need validation on hardware. 16-bit audio <-> 12-bit analog
 * is centered at mid-scale (2048).
 */
#include "audio_sai.h"
#include <libopencm3/cm3/common.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/dac.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/timer.h>

#define AUD_RING 4

/* playback (loop producer / DAC-DMA-ISR consumer) — DMA holds 12-bit values */
static int16_t       tx_ring[AUD_RING][AUD_FRAME];
static volatile int  tx_head, tx_tail;
static uint16_t      tx_dma[2 * AUD_FRAME];

/* capture (ADC-DMA-ISR producer / loop consumer) */
static uint16_t      rx_dma[2 * AUD_FRAME];
static int16_t       rx_frame[AUD_FRAME];
static volatile int  rx_ready;

static inline uint16_t s16_to_dac12(int16_t s) { return (uint16_t) (((int) s + 32768) >> 4); }
static inline int16_t  adc12_to_s16(uint16_t a) { return (int16_t) (((int) a << 4) - 32768); }

/* TIM6 TRGO at ~48 kHz to pace the DAC (prescaler/period tuned on board). */
static void tim6_48k(void)
{
    rcc_periph_clock_enable(RCC_TIM6);
    timer_set_prescaler(TIM6, 0);
    timer_set_period(TIM6, 1124);                 /* ~48 kHz @ 54 MHz APB1 timer clk */
    timer_set_master_mode(TIM6, TIM_CR2_MMS_UPDATE);
    timer_enable_counter(TIM6);
}

void aud_spk_start(void)
{
    rcc_periph_clock_enable(RCC_DAC);
    rcc_periph_clock_enable(RCC_DMA1);
    tim6_48k();

    /* DMA1 stream5 ch7 -> DAC_DHR12R1, circular double buffer. */
    dma_stream_reset(DMA1, DMA_STREAM5);
    dma_channel_select(DMA1, DMA_STREAM5, DMA_SxCR_CHSEL_7);
    dma_set_peripheral_address(DMA1, DMA_STREAM5, (uint32_t) &DAC_DHR12R1(DAC1));
    dma_set_memory_address(DMA1, DMA_STREAM5, (uint32_t) tx_dma);
    dma_set_number_of_data(DMA1, DMA_STREAM5, 2 * AUD_FRAME);
    dma_set_transfer_mode(DMA1, DMA_STREAM5, DMA_SxCR_DIR_MEM_TO_PERIPHERAL);
    dma_set_peripheral_size(DMA1, DMA_STREAM5, DMA_SxCR_PSIZE_16BIT);
    dma_set_memory_size(DMA1, DMA_STREAM5, DMA_SxCR_MSIZE_16BIT);
    dma_enable_memory_increment_mode(DMA1, DMA_STREAM5);
    dma_enable_circular_mode(DMA1, DMA_STREAM5);
    dma_enable_half_transfer_interrupt(DMA1, DMA_STREAM5);
    dma_enable_transfer_complete_interrupt(DMA1, DMA_STREAM5);
    dma_enable_stream(DMA1, DMA_STREAM5);

    dac_set_trigger_source(DAC1, DAC_CR_TSEL1_T6);
    dac_trigger_enable(DAC1, DAC_CHANNEL1);
    dac_dma_enable(DAC1, DAC_CHANNEL1);
    dac_enable(DAC1, DAC_CHANNEL1);
}

static void tx_refill(int half)
{
    uint16_t *dst = &tx_dma[half * AUD_FRAME];
    if (tx_tail != tx_head) {
        for (int i = 0; i < AUD_FRAME; i++) dst[i] = s16_to_dac12(tx_ring[tx_tail][i]);
        tx_tail = (tx_tail + 1) % AUD_RING;
    } else {
        for (int i = 0; i < AUD_FRAME; i++) dst[i] = 2048;   /* underrun -> mid-scale */
    }
}

void dma1_stream5_isr(void)
{
    if (dma_get_interrupt_flag(DMA1, DMA_STREAM5, DMA_HTIF)) {
        dma_clear_interrupt_flags(DMA1, DMA_STREAM5, DMA_HTIF);
        tx_refill(0);
    }
    if (dma_get_interrupt_flag(DMA1, DMA_STREAM5, DMA_TCIF)) {
        dma_clear_interrupt_flags(DMA1, DMA_STREAM5, DMA_TCIF);
        tx_refill(1);
    }
}

void aud_spk_push_raw(const int16_t *frame)
{
    int next = (tx_head + 1) % AUD_RING;
    if (next != tx_tail) {
        for (int i = 0; i < AUD_FRAME; i++) tx_ring[tx_head][i] = frame[i];
        tx_head = next;
    }
}

void aud_mic_start(void)
{
    rcc_periph_clock_enable(RCC_ADC1);
    rcc_periph_clock_enable(RCC_DMA2);

    /* ADC1 single channel, continuous, DMA circular into rx_dma. */
    uint8_t channels[1] = { 0 };
    adc_power_off(ADC1);
    adc_set_resolution(ADC1, ADC_CR1_RES_12BIT);
    adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_15CYC);
    adc_set_regular_sequence(ADC1, 1, channels);
    adc_set_continuous_conversion_mode(ADC1);
    adc_enable_dma(ADC1);

    dma_stream_reset(DMA2, DMA_STREAM0);
    dma_channel_select(DMA2, DMA_STREAM0, DMA_SxCR_CHSEL_0);
    dma_set_peripheral_address(DMA2, DMA_STREAM0, (uint32_t) &ADC_DR(ADC1));
    dma_set_memory_address(DMA2, DMA_STREAM0, (uint32_t) rx_dma);
    dma_set_number_of_data(DMA2, DMA_STREAM0, 2 * AUD_FRAME);
    dma_set_transfer_mode(DMA2, DMA_STREAM0, DMA_SxCR_DIR_PERIPHERAL_TO_MEM);
    dma_set_peripheral_size(DMA2, DMA_STREAM0, DMA_SxCR_PSIZE_16BIT);
    dma_set_memory_size(DMA2, DMA_STREAM0, DMA_SxCR_MSIZE_16BIT);
    dma_enable_memory_increment_mode(DMA2, DMA_STREAM0);
    dma_enable_circular_mode(DMA2, DMA_STREAM0);
    dma_enable_half_transfer_interrupt(DMA2, DMA_STREAM0);
    dma_enable_transfer_complete_interrupt(DMA2, DMA_STREAM0);
    dma_enable_stream(DMA2, DMA_STREAM0);

    adc_power_on(ADC1);
    adc_start_conversion_regular(ADC1);
}

static void rx_capture(int half)
{
    for (int i = 0; i < AUD_FRAME; i++) rx_frame[i] = adc12_to_s16(rx_dma[half * AUD_FRAME + i]);
    rx_ready = 1;
}

void dma2_stream0_isr(void)
{
    if (dma_get_interrupt_flag(DMA2, DMA_STREAM0, DMA_HTIF)) {
        dma_clear_interrupt_flags(DMA2, DMA_STREAM0, DMA_HTIF);
        rx_capture(0);
    }
    if (dma_get_interrupt_flag(DMA2, DMA_STREAM0, DMA_TCIF)) {
        dma_clear_interrupt_flags(DMA2, DMA_STREAM0, DMA_TCIF);
        rx_capture(1);
    }
}

int aud_mic_ready(void) { return rx_ready; }

void aud_mic_take_raw(int16_t *out)
{
    rx_ready = 0;
    for (int i = 0; i < AUD_FRAME; i++) out[i] = rx_frame[i];
}
