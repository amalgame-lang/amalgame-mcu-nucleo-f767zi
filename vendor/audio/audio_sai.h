#ifndef MCNET_AUDIO_SAI_H
#define MCNET_AUDIO_SAI_H

#include <stdint.h>

/* 48 kHz, 120-sample (2.5 ms) int16 mono frames — matches the RTP/jitter core. */
#define AUD_FRAME 120

/* Playback: SAI block A (master TX) + DMA circular double buffer. */
void aud_spk_start(void);
void aud_spk_push_raw(const int16_t *frame);   /* enqueue AUD_FRAME samples (BLOCKS while ring full) */
int  aud_spk_free(void);                        /* frames push_raw can take right now without blocking */
unsigned aud_spk_underruns(void);               /* DMA refills that found the ring empty (silence played) */

/* Capture: SAI block B (master RX) + DMA circular double buffer. */
void aud_mic_start(void);
int  aud_mic_ready(void);                       /* 1 if a captured frame waits */
void aud_mic_take_raw(int16_t *out);            /* copy AUD_FRAME samples out  */
unsigned aud_mic_overruns(void);                /* captures that overwrote a frame not yet taken (lost) */
unsigned aud_mic_frames(void);                  /* capture ISR count = frames the DMA produced (audio clock) */
unsigned aud_spk_frames(void);                  /* playback refill ISR count (audio clock) */
unsigned long long aud_mic_last_us(void);       /* net_micros() taken IN the capture ISR (0 if no net_mcu linked) */

#endif /* MCNET_AUDIO_SAI_H */
