/* Board clock bring-up for the Nucleo-F767ZI (and boards derived from it).
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
#include <libopencm3/stm32/gpio.h>
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
     * 344/7 = 49.1429 MHz (NOT exactly 49.152: integer PLL). SAI MCKDIV=2 → MCLK 12.2857 MHz,
     * 64-bit frame → BCK 3.0714 MHz, LRCK = 47 991 Hz (-186 ppm vs 48 kHz; identical on every
     * board, so irrelevant box-to-box — matters only against an external 48 kHz reference such
     * as USB audio: a board may feed an exact 12.288 MHz oscillator on I2S_CKIN/PC9, MC_AUDIO_CKIN). */
    RCC_DCKCFGR1 &= ~(0x1Fu << DCKCFGR1_PLLSAIDIVQ_SHIFT);   /* DIVQ = /1 */
    RCC_DCKCFGR1 &= ~(0x3u  << DCKCFGR1_SAI1SEL_SHIFT);      /* SAI1SEL = PLLSAI */

    RCC_CR |= RCC_CR_PLLSAION;
    while (!(RCC_CR & RCC_CR_PLLSAIRDY)) { }
}

/* Relecture du 2026-10-26 : rcc_clock_setup_hse() attend le HSE SANS limite. Quartz absent, mal soudé ou qui
 * ne démarre pas → boucle infinie, le chien de garde du chargeur relance, et ainsi de suite : boîtier mort, sans
 * un mot. Ici on lui laisse ≈ 0,5 s (démarrage typique d'un quartz 8 MHz : 2 ms), sinon repli sur le HSI 16 MHz
 * (même 216 MHz, PLLM = 16 → même 1 MHz d'entrée VCO, donc le PLLSAI ci-dessous reste juste ; précision ±1 % :
 * l'Ethernet tire son horloge du PHY, l'audio de l'oscillateur externe avec MC_AUDIO_CKIN — seule la console série
 * en souffre). */
int board_hse_ok;          /* 0 = repli HSI : visible dans le diagnostic */
int board_audio_ckin;      /* 1 = SAI1 sur l'oscillateur externe (I2S_CKIN) ; 0 = PLLSAI (audio_sai.c choisit MCKDIV) */
static int hse_start(void)
{
    rcc_osc_on(RCC_HSE);
    for (volatile uint32_t i = 0; i < 1000000u; i++) if (RCC_CR & RCC_CR_HSERDY) return 1;   /* rcc_is_osc_ready() non implémenté pour le F7 dans libopencm3 */
    rcc_osc_off(RCC_HSE);
    return 0;
}
#if defined(MC_AUDIO_CKIN) && MC_AUDIO_CKIN
/* L'oscillateur externe bat-il ? En mode AF, l'IDR suit toujours la broche : à 12,288 MHz, une lecture en boucle voit
 * forcément des 0 ET des 1. Broche figée (oscillateur absent ou non alimenté) → on reste sur le PLLSAI
 * au lieu d'un SAI sans horloge, donc muet (relecture du 2026-10-26). */
static int ckin_alive(void)
{
    int hi = 0, lo = 0;
    for (int i = 0; i < 4000 && !(hi && lo); i++) { if (gpio_get(GPIOC, GPIO9)) hi = 1; else lo = 1; }
    return hi && lo;
}
#endif

void Board_ClockInit(void)
{
    /* 1) 216 MHz system clock from the 8 MHz HSE (repli HSI si le quartz ne démarre pas). */
    board_hse_ok = hse_start();
    if (board_hse_ok) rcc_clock_setup_hse(&rcc_3v3[RCC_CLOCK_3V3_216MHZ], 8);
    else rcc_clock_setup_hsi(&rcc_3v3[RCC_CLOCK_3V3_216MHZ]);

    /* 2) audio kernel clock. */
    pllsai_audio_48k();
#if defined(MC_AUDIO_CKIN) && MC_AUDIO_CKIN
    /* Option MC_AUDIO_CKIN (2026-09-13), carte portant un oscillateur 12,288 MHz sur PC9 = I2S_CKIN (AF5,
     * DS11532 table des fonctions alternatives). SAI1 kernel = I2S_CKIN : RCC_DCKCFGR1.SAI1SEL = 10, ce que le
     * HAL ST nomme RCC_SAI1CLKSOURCE_PIN (stm32f7xx_hal_rcc_ex.h). Avec MCKDIV = 0 dans audio_sai.c (÷1,
     * formule HAL MCLK = SAI_CK/(2·MCKDIV), MCKDIV = 12,288/(48 k·512) = 0,5 → 0), MCLK = 12,288 MHz EXACT,
     * BCK = 3,072 MHz, LRCK = 48 000,0 Hz (contre 47 991 Hz avec le PLLSAI entier 344/7). Le PLLSAI ci-dessus
     * reste configuré : repli immédiat en remettant SAI1SEL = 00. Sans MC_AUDIO_CKIN (Nucleo-F767ZI, pas
     * d'oscillateur) rien ne change. À VÉRIFIER à l'analyseur au bring-up de la carte (LRCK 48 000 Hz). */
    rcc_periph_clock_enable(RCC_GPIOC);
    gpio_mode_setup(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO9);
    gpio_set_af(GPIOC, GPIO_AF5, GPIO9);
    for (volatile int i = 0; i < 20000; i++) { }                 /* ≈ 1 ms : démarrage de l'oscillateur après la mise sous tension */
    board_audio_ckin = ckin_alive();
    if (board_audio_ckin) RCC_DCKCFGR1 = (RCC_DCKCFGR1 & ~(0x3u << DCKCFGR1_SAI1SEL_SHIFT)) | (0x2u << DCKCFGR1_SAI1SEL_SHIFT);
#endif

    /* 3) 1 kHz SysTick (216 MHz AHB / 216000 = 1 kHz). */
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
    systick_set_reload(216000u - 1u);
    systick_clear();
    systick_counter_enable();
    systick_interrupt_enable();
}
