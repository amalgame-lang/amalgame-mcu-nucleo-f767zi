/* OTA slot B image: 512-byte signed header (tools/ota-sign.py fills it) then the vector table, 512 KB. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x080C0000, LENGTH = 512K
  RAM  (rwx) : ORIGIN = 0x20020000, LENGTH = 384K
  DTCM (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
}
_estack = ORIGIN(DTCM) + LENGTH(DTCM);
SECTIONS
{
  .fwhdr      : { KEEP(*(.fwhdr)) . = 512; } > FLASH
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text       : { *(.text*) *(.rodata*) } > FLASH
  _sidata = LOADADDR(.data);
  .data : { _sdata = .; *(.data*) _edata = .; } > RAM AT > FLASH
  .bss  : { _sbss = .; *(.bss*) *(COMMON) _ebss = .; } > RAM
  .dtcm (NOLOAD) : { *(.dtcm*) } > DTCM
}
