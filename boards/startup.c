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
void hard_fault_handler(void);   /* vendor/boot/fault.c */
void mem_manage_handler(void);
void bus_fault_handler(void);
void usage_fault_handler(void);

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
void usart3_isr(void)         __attribute__((weak, alias("Default_Handler")));   /* console TX ring (Amalgame_Mcu_Board.h) */

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
    [16 + 39] = usart3_isr,               /* USART3 — console TX ring       */
    /* HardFault : sans ce gestionnaire, une faute tombe dans Default_Handler (boucle infinie) et
     * il faut attendre le chien de garde SANS savoir pourquoi. Ici on enregistre le contexte en
     * .noinit puis on redémarre tout de suite (vendor/boot/fault.c). Un index explicite APRÈS
     * l'initialiseur de plage [2 ... 14] le remplace bien. */
    [3]  = hard_fault_handler,            /* HardFault                      */
    [4]  = mem_manage_handler,            /* MemManage                      */
    [5]  = bus_fault_handler,             /* BusFault                       */
    [6]  = usage_fault_handler,           /* UsageFault                     */
    [16 + 61] = eth_isr,                  /* ETH                            */
    [16 + 68] = dma2_stream5_isr,         /* DMA2_STREAM5 — SAI block B RX  */
};

/* Enable the FPU coprocessor (CP10+CP11 full access) before any code runs. Without
 * this, ANY compiler-emitted VFP instruction (explicit float math, or GCC picking a
 * VLDR/VSTR block copy for e.g. a growing List<int>'s realloc) traps as a UsageFault
 * that escalates to HardFault (Default_Handler = infinite loop, so total silence —
 * no banner, nothing). -mfloat-abi=hard -mfpu=fpv5-d16 is used project-wide, so this
 * must run first, unconditionally — not just for firmware that "uses floats". */
#define SCB_CPACR (*(volatile uint32_t *) 0xE000ED88u)
/* L1 instruction cache (Cortex-M7). Code lives in flash on the AXI bus at 0x0800 0000, where the ART
 * accelerator does NOT apply (ART only serves the ITCM alias at 0x0020 0000): without the I-cache
 * every fetch pays the flash wait states and tight loops crawl — measured 2026-09-05: SHA-256 at
 * 246 cycles/byte, a 650-byte TLS record in 5.8 ms. The I-cache has no coherence issue with DMA
 * (DMA never writes code) and the D-cache stays OFF (audio/ETH buffers are DMA targets).
 * -DBOARD_NO_ICACHE=1 to compare. */
#define SCB_CCR     (*(volatile uint32_t *) 0xE000ED14u)
#define SCB_ICIALLU (*(volatile uint32_t *) 0xE000EF50u)
static void ICache_Enable(void) {
#ifndef BOARD_NO_ICACHE
    __asm volatile ("dsb"); __asm volatile ("isb");
    SCB_ICIALLU = 0u;                       /* invalidate */
    __asm volatile ("dsb"); __asm volatile ("isb");
    SCB_CCR |= (1u << 17);                  /* IC = 1 */
    __asm volatile ("dsb"); __asm volatile ("isb");
#endif
}
static void Fpu_Enable(void) {
    SCB_CPACR |= (0xFu << 20);   /* CP10 + CP11: full access */
    __asm volatile ("dsb");
    __asm volatile ("isb");
}

#define SCB_VTOR (*(volatile uint32_t *) 0xE000ED08u)
#ifdef MC_SLOT
/* OTA slot image: 512-byte header placeholder at the slot base (tools/ota-sign.py overwrites it in the .bin). */
__attribute__((section(".fwhdr"), used)) const uint8_t fw_header_space[512] = { 0 };
#endif
void Reset_Handler(void) {
    SCB_VTOR = (uint32_t) (uintptr_t) vector_table;   /* this image's table (a slot image is not at 0x08000000) */
    Fpu_Enable();
    ICache_Enable();
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;
    Board_ClockInit();        /* weak no-op unless clock.c is linked */
    amc_main();
    for (;;) {}
}
