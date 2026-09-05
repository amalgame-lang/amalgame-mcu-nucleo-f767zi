/* Bootloader: sectors 0-2 (96 KB) at the flash origin; DTCM for stack, SRAM for data/bss. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 96K
  RAM  (rwx) : ORIGIN = 0x20020000, LENGTH = 384K
  DTCM (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
}
_estack = ORIGIN(DTCM) + LENGTH(DTCM);
SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text       : { *(.text*) *(.rodata*) } > FLASH
  _sidata = LOADADDR(.data);
  .data : { _sdata = .; *(.data*) _edata = .; } > RAM AT > FLASH
  .bss  : { _sbss = .; *(.bss*) *(COMMON) _ebss = .; } > RAM
  .dtcm (NOLOAD) : { *(.dtcm*) } > DTCM
}
