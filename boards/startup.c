/*
 * Minimal Cortex-M7 startup for STM32F767ZI. Owns the reset path; calls
 * amc_main() (the entry amc emits for an embedded build), then idles.
 *
 * NOTE: simplified vector table (core exceptions only). A real application
 * that uses peripheral interrupts (SysTick, EXTI, …) needs the full STM32F7
 * IRQ vector table — easiest via libopencm3's own startup/linker script.
 * This file is a scaffold for the bring-up blink; validate before relying on it.
 */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int amc_main(void);

void Reset_Handler(void);
void Default_Handler(void) { for (;;) {} }

__attribute__((section(".isr_vector"), used))
uint32_t *const vector_table[] = {
    (uint32_t *)&_estack,
    (uint32_t *)Reset_Handler,
    (uint32_t *)Default_Handler,  /* NMI       */
    (uint32_t *)Default_Handler,  /* HardFault */
    (uint32_t *)Default_Handler,  /* MemManage */
    (uint32_t *)Default_Handler,  /* BusFault  */
    (uint32_t *)Default_Handler,  /* UsageFault*/
};

void Reset_Handler(void) {
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;
    amc_main();
    for (;;) {}
}
