#ifndef MCNET_AUDIO_SAI_H
#define MCNET_AUDIO_SAI_H

#include <stdint.h>

/* 48 kHz, 120-sample (2.5 ms) int16 mono frames — matches the RTP/jitter core. */
#define AUD_FRAME 120

/* Playback: SAI block A (master TX) + DMA circular double buffer. */
void aud_spk_start(void);
void aud_spk_push_raw(const int16_t *frame);   /* enqueue AUD_FRAME samples */

/* Capture: SAI block B (master RX) + DMA circular double buffer. */
void aud_mic_start(void);
int  aud_mic_ready(void);                       /* 1 if a captured frame waits */
void aud_mic_take_raw(int16_t *out);            /* copy AUD_FRAME samples out  */

#endif /* MCNET_AUDIO_SAI_H */
