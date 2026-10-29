#ifndef MCNET_AUDIO_SAI_H
#define MCNET_AUDIO_SAI_H

#include <stdint.h>

/* 48 kHz int16 mono frames. Frame length is a BUILD parameter shared with the AM
 * core (rtp_core.am reads the same MC_FRAME): 120 = 2.5 ms (default), 48 = 1 ms. */
#ifndef MC_FRAME
#define MC_FRAME 120
#endif
#define AUD_FRAME MC_FRAME
/* Software rings (frames) on both sides of the DMA double buffers: 4 = 3 queued =
 * 7.5 ms of slack at 2.5 ms frames; 2 = 1 queued = lowest latency, needs a loop
 * that never runs late by a full frame. -DAUD_RING= */
#ifndef AUD_RING
#define AUD_RING 4
#endif

/* Playback: SAI block A (master TX) + DMA circular double buffer. */
void aud_spk_start(void);
void aud_spk_push_raw(const int16_t *frame);   /* enqueue AUD_FRAME samples (BLOCKS while ring full) */
int  aud_spk_free(void);                        /* frames push_raw can take right now without blocking */
unsigned aud_spk_underruns(void);               /* DMA refills that found the ring empty (silence played) */

/* Capture: SAI block B (master RX) + DMA circular double buffer. */
void aud_mic_start(void);
int  aud_mic_ready(void);                       /* 1 if a captured frame waits */
void aud_mic_take_raw(int16_t *out);            /* copy AUD_FRAME samples of the LEFT input (mic) */
void aud_mic_take_raw2(int16_t *l, int16_t *r); /* both inputs: L = VINL (mic), R = VINR (instrument) */
unsigned aud_mic_overruns(void);                /* captures that overwrote a frame not yet taken (lost) */
unsigned aud_mic_frames(void);                  /* capture ISR count = frames the DMA produced (audio clock) */
unsigned aud_spk_frames(void);                  /* playback refill ISR count (audio clock) */
unsigned long long aud_mic_last_us(void);       /* net_micros() taken IN the capture ISR (0 if no net_mcu linked) */

#endif /* MCNET_AUDIO_SAI_H */
