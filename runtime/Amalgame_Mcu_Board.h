/*
 * amalgame-mcu-nucleo-f767zi — Amalgame.Mcu HAL for ST Nucleo-F767ZI
 * (STM32F767ZI, Cortex-M7F). Wraps libopencm3 (opencm3_stm32f7).
 *
 * Defining AMC_HAVE_MCU_BOARD suppresses the virtual/semihosting default in
 * runtime/embedded/_runtime.h and provides the REAL Mcu_* bodies. Include
 * this header before "_runtime.h" (the board build arranges the -I order).
 *
 * STATUS: scaffold — written against the libopencm3 API but NOT yet built or
 * flashed (no board/toolchain on the authoring machine). Treat as a starting
 * point; verify pin map + clock setup against your wiring before trusting.
 *
 * Pin encoding (matches the int-based Mcu API, slice 6): pin = port*16 + bit,
 * port 0=A,1=B,2=C,... So PB0 (green LD1) = 1*16+0 = 16. Named constants:
 */
#ifndef AMALGAME_MCU_BOARD_F767ZI_H
#define AMALGAME_MCU_BOARD_F767ZI_H
#define AMC_HAVE_MCU_BOARD 1

#include <stdint.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>

/* Nucleo-F767ZI on-board LEDs / button (UM1974). */
#define Board_LedGreen ((i64)(1*16 + 0))   /* PB0  — LD1 */
#define Board_LedBlue  ((i64)(1*16 + 7))   /* PB7  — LD2 */
#define Board_LedRed   ((i64)(1*16 + 14))  /* PB14 — LD3 */
#define Board_Button   ((i64)(2*16 + 13))  /* PC13 — B1 (user) */
#define Board_LedBuiltin Board_LedGreen

static const uint32_t amc_gpio_ports[11] = {
    GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG, GPIOH, GPIOI, GPIOJ, GPIOK
};
static const enum rcc_periph_clken amc_gpio_clk[11] = {
    RCC_GPIOA, RCC_GPIOB, RCC_GPIOC, RCC_GPIOD, RCC_GPIOE, RCC_GPIOF,
    RCC_GPIOG, RCC_GPIOH, RCC_GPIOI, RCC_GPIOJ, RCC_GPIOK
};
static inline uint32_t amc_port_of(i64 pin) { return amc_gpio_ports[(pin >> 4) & 0xF]; }
static inline uint16_t amc_bit_of(i64 pin)  { return (uint16_t)(1u << (pin & 0xF)); }

static inline int  Mcu_High(void)   { return 1; }
static inline int  Mcu_Low(void)    { return 0; }
static inline int  Mcu_Output(void) { return 1; }
static inline int  Mcu_Input(void)  { return 0; }

static inline void Mcu_PinMode(i64 pin, i64 mode) {
    rcc_periph_clock_enable(amc_gpio_clk[(pin >> 4) & 0xF]);
    gpio_mode_setup(amc_port_of(pin),
                    mode ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
                    GPIO_PUPD_NONE, amc_bit_of(pin));
}
static inline void Mcu_DigitalWrite(i64 pin, i64 level) {
    if (level) { gpio_set(amc_port_of(pin), amc_bit_of(pin)); }
    else       { gpio_clear(amc_port_of(pin), amc_bit_of(pin)); }
}
static inline i64 Mcu_DigitalRead(i64 pin) {
    return gpio_get(amc_port_of(pin), amc_bit_of(pin)) ? 1 : 0;
}
static inline void Mcu_Toggle(i64 pin) { gpio_toggle(amc_port_of(pin), amc_bit_of(pin)); }

/* Crude busy-wait delay. Replace with SysTick for accuracy. Assumes the
 * default ~16 MHz HSI unless you configure the PLL in setup{}. */
static inline void Mcu_DelayMs(i64 ms) {
    for (volatile i64 i = 0; i < ms * 4000; i++) { __asm__ volatile("nop"); }
}
static inline i64 Mcu_Millis(void) { return 0; /* TODO: SysTick counter */ }

/* Console over USART3 (PD8 TX / PD9 RX) = the on-board ST-LINK virtual COM
 * port (/dev/ttyACM0 @ 115200). Overrides the semihosting Console default so
 * Console.WriteLine works on real silicon (no debugger needed) — read it with
 * `amc monitor`. Lazy-inits on first write. Baud assumes the 16 MHz HSI reset
 * clock (no PLL setup); fine for logging. */
#define AMC_HAVE_CONSOLE 1
static int amc_uart_inited = 0;
static void amc_uart_init(void) {
    rcc_periph_clock_enable(RCC_GPIOD);
    rcc_periph_clock_enable(RCC_USART3);
    gpio_mode_setup(GPIOD, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO8 | GPIO9);
    gpio_set_af(GPIOD, GPIO_AF7, GPIO8 | GPIO9);
    rcc_apb1_frequency = 16000000;   /* HSI reset clock, so baud computes right */
    usart_set_baudrate(USART3, 115200);
    usart_set_databits(USART3, 8);
    usart_set_stopbits(USART3, USART_STOPBITS_1);
    usart_set_mode(USART3, USART_MODE_TX);
    usart_set_parity(USART3, USART_PARITY_NONE);
    usart_set_flow_control(USART3, USART_FLOWCONTROL_NONE);
    usart_enable(USART3);
    amc_uart_inited = 1;
}
static inline void code_putc(char c) {
    if (!amc_uart_inited) { amc_uart_init(); }
    usart_send_blocking(USART3, (uint8_t)c);
}
static inline void Console_Write(code_string s) {
    if (!s) { return; }
    if (!amc_uart_inited) { amc_uart_init(); }
    for (const char* p = s; *p; p++) { usart_send_blocking(USART3, (uint8_t)*p); }
}
static inline void Console_WriteLine(code_string s) {
    Console_Write(s);
    usart_send_blocking(USART3, (uint8_t)'\r');
    usart_send_blocking(USART3, (uint8_t)'\n');
}

#endif /* AMALGAME_MCU_BOARD_F767ZI_H */
