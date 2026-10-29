/* STM32F767ZI — 2 MB flash @ 0x08000000.
 * RAM lives in the AHB-accessible SRAM1+SRAM2 region (0x20020000, 384 KB), NOT the
 * 128 KB DTCM at 0x20000000: the Ethernet DMA (and the DMA1/DMA2 controllers used by
 * the SAI/DAC/ADC audio path) CANNOT reach DTCM on the STM32F7 — descriptors/buffers
 * there cause a HardFault / silent no-RX. Putting all .data/.bss/stack in SRAM keeps
 * every DMA buffer reachable. (DTCM is left free for future CPU-only hot data.)
 * Minimal script; for production you may prefer libopencm3's device script. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 2048K
  RAM  (rwx) : ORIGIN = 0x20020000, LENGTH = 384K   /* SRAM1+SRAM2 (DMA-reachable) */
  DTCM (rw)  : ORIGIN = 0x20000000, LENGTH = 128K   /* CPU-only, zero wait-state: hot data placed there explicitly (.dtcm) */
}

/* Stack in DTCM (0 wait-state; mbedTLS/bignum temporaries and every hot local benefit). No DMA buffer may live on the stack:
 * lwIP copies (TCP_WRITE_FLAG_COPY), the audio/ETH buffers are static in SRAM. Measured 2026-09-05 (TLS handshake). */
_estack = ORIGIN(DTCM) + LENGTH(DTCM);

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text       : { *(.text*) *(.rodata*) } > FLASH

  _sidata = LOADADDR(.data);
  .data : { _sdata = .; *(.data*) _edata = .; } > RAM AT > FLASH
  .bss  : { _sbss = .; *(.bss*) *(COMMON) _ebss = .; } > RAM
  /* .noinit : APRÈS .bss, donc HORS de la plage _sbss.._ebss que startup.c remet à zéro. C'est ce
     qui permet à l'enregistrement de faute de survivre au redémarrage et de dire, au boot suivant,
     POURQUOI la carte est tombée. Perdu en cas de coupure d'alimentation — acceptable : là,
     quelqu'un est intervenu physiquement. */
  .noinit (NOLOAD) : { *(.noinit*) } > RAM
  /* NOLOAD, not zeroed by startup: only for buffers whose users initialise them (mbedTLS pool: calloc memsets). NEVER a DMA buffer. */
  .dtcm (NOLOAD) : { *(.dtcm*) } > DTCM
}
