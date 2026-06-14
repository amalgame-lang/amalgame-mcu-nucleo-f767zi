/* Board clock bring-up for MusiCall-Box on the Nucleo-F767ZI.
 *
 * Provides the STRONG Board_ClockInit() (startup.c has a weak no-op default, so
 * blink — which doesn't link this file — keeps running on the 16 MHz HSI exactly as
 * validated; only firmware that links clock.c gets the full clock tree).
 *
 *   1) SYSCLK 216 MHz from the 8 MHz HSE (ST-LINK MCO) — gives HCLK for the Ethernet
 *      RMII (needs >=25 MHz) and headroom for audio + the FPU.
 *   2) PLLSAI → SAI1 kernel clock for the 12.288 MHz MCLK (48 kHz x 256) the PCM5102
 *      DAC / PCM1802 ADC need. Register values are the documented intent; the exact
 *      audio divider must be trimmed against a scope on hardware.
 *   3) SysTick at 1 kHz → sys_now() (lwIP) via sys_tick_handler (net_mcu.c).
 *
 * SCAFFOLD: link-proves; clock frequencies + PLLSAI lock need on-board validation.
 */
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/cm3/systick.h>

/* RCC bits used for the PLLSAI audio clock (raw MMIO — names stable across f7). */
#define PLLSAICFGR_PLLSAIN_SHIFT  6
#define PLLSAICFGR_PLLSAIQ_SHIFT  24
#define DCKCFGR1_PLLSAIDIVQ_SHIFT 8
#define DCKCFGR1_SAI1SEL_SHIFT    20

static void pllsai_audio_48k(void)
{
    /* VCO_in = HSE/PLLM = 8 MHz / 8 = 1 MHz (PLLM shared with the main PLL preset).
     * Target SAI clock ~= 12.288 MHz: VCO = 1 MHz * PLLSAIN, /PLLSAIQ /PLLSAIDIVQ.
     * Example: PLLSAIN=344 -> 344 MHz, /PLLSAIQ=7 -> 49.14 MHz, /PLLSAIDIVQ=4 ->
     * 12.288 MHz. TRIM on board (a scope on MCLK/PE2). */
    RCC_CR &= ~RCC_CR_PLLSAION;
    while (RCC_CR & RCC_CR_PLLSAIRDY) { }

    RCC_PLLSAICFGR = (344u << PLLSAICFGR_PLLSAIN_SHIFT)
                   | (7u   << PLLSAICFGR_PLLSAIQ_SHIFT);

    /* SAI1 clock = PLLSAI (SAI1SEL=00), PLLSAIDIVQ = /1 (field 0) → SAI kernel =
     * 344/7 ≈ 49.152 MHz. With NODIV=1 + 32-bit frame, SCK = 49.152/32 = 1.536 MHz,
     * LRCK = 48 kHz (in the PCM5102A PLL lock range). */
    RCC_DCKCFGR1 &= ~(0x1Fu << DCKCFGR1_PLLSAIDIVQ_SHIFT);   /* DIVQ = /1 */
    RCC_DCKCFGR1 &= ~(0x3u  << DCKCFGR1_SAI1SEL_SHIFT);      /* SAI1SEL = PLLSAI */

    RCC_CR |= RCC_CR_PLLSAION;
    while (!(RCC_CR & RCC_CR_PLLSAIRDY)) { }
}

void Board_ClockInit(void)
{
    /* 1) 216 MHz system clock from the 8 MHz HSE. */
    rcc_clock_setup_hse(&rcc_3v3[RCC_CLOCK_3V3_216MHZ], 8);

    /* 2) audio kernel clock. */
    pllsai_audio_48k();

    /* 3) 1 kHz SysTick (216 MHz AHB / 216000 = 1 kHz). */
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
    systick_set_reload(216000u - 1u);
    systick_clear();
    systick_counter_enable();
    systick_interrupt_enable();
}
