/*
 * Cortex-M7 startup for STM32F767ZI. Owns the reset path, runs the (optional) board
 * clock init, then calls amc_main() (the entry amc emits for an embedded build).
 *
 * Full STM32F7 vector table (16 core + 104 IRQ slots). Every IRQ defaults to
 * Default_Handler via a weak alias, and the handlers we use (SysTick + the audio/net
 * DMA streams + ETH) are weak-aliased here too — so a firmware that links the matching
 * driver (audio_sai.c / analog_audio.c / net_mcu.c) overrides them, while a firmware
 * that doesn't (e.g. blink) still links cleanly against the defaults.
 *
 * Board_ClockInit() is a WEAK no-op here: blink (which doesn't link clock.c) stays on
 * the 16 MHz HSI exactly as validated; firmware that links clock.c gets the strong
 * 216 MHz + PLLSAI + SysTick bring-up.
 */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int amc_main(void);

void Reset_Handler(void);
void Default_Handler(void) { for (;;) {} }

/* Weak no-op clock init; clock.c provides the strong version. */
__attribute__((weak)) void Board_ClockInit(void) {}

/* SysTick is ENABLED by clock.c independently of any driver, so its default must be a
 * harmless no-op (return), not the Default_Handler trap — net_mcu.c overrides it when
 * linked (lwIP's ms tick). */
__attribute__((weak)) void sys_tick_handler(void) {}

/* DMA handlers — weak trap default; only fire when their driver (which defines the
 * strong handler) is linked, so the trap catches genuinely unexpected interrupts. */
void dma1_stream5_isr(void)   __attribute__((weak, alias("Default_Handler")));
void dma2_stream0_isr(void)   __attribute__((weak, alias("Default_Handler")));
void dma2_stream1_isr(void)   __attribute__((weak, alias("Default_Handler")));
void dma2_stream5_isr(void)   __attribute__((weak, alias("Default_Handler")));
void eth_isr(void)            __attribute__((weak, alias("Default_Handler")));

typedef void (*vector_fn)(void);

/* 16 core exception slots + 104 STM32F7 peripheral IRQs (NVIC_IRQ_COUNT). */
__attribute__((section(".isr_vector"), used))
vector_fn const vector_table[16 + 104] = {
    [0]  = (vector_fn) (uintptr_t) &_estack,
    [1]  = Reset_Handler,
    [2 ... 14]   = Default_Handler,       /* NMI..SysTick-1 (core exceptions) */
    [15] = sys_tick_handler,              /* SysTick */
    [16 ... 119] = Default_Handler,       /* all peripheral IRQs default */
    /* wired IRQs (index = 16 + NVIC number) */
    [16 + 16] = dma1_stream5_isr,         /* DMA1_STREAM5 — analog DAC TX  */
    [16 + 56] = dma2_stream0_isr,         /* DMA2_STREAM0 — analog ADC RX  */
    [16 + 57] = dma2_stream1_isr,         /* DMA2_STREAM1 — SAI block A TX  */
    [16 + 61] = eth_isr,                  /* ETH                            */
    [16 + 68] = dma2_stream5_isr,         /* DMA2_STREAM5 — SAI block B RX  */
};

void Reset_Handler(void) {
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;
    Board_ClockInit();        /* weak no-op unless clock.c is linked */
    amc_main();
    for (;;) {}
}
