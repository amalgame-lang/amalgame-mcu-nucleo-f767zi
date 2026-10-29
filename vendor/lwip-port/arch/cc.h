#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

/* Minimal lwIP compiler/arch port for arm-none-eabi (newlib), NO_SYS. */
#include <stdint.h>
#include <stddef.h>

/* STM32 is little-endian. */
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

/* No host unistd on bare metal. */
#define LWIP_NO_UNISTD_H 1
#define LWIP_TIMEVAL_PRIVATE 0

/* Diagnostics dropped; assert traps (no console dependency at link-proof time). */
#define LWIP_PLATFORM_DIAG(x)
#define LWIP_PLATFORM_ASSERT(x) do { for (;;) {} } while (0)

/* DHCP xid / timing jitter. Provided by net_mcu.c. */
uint32_t lwip_rand(void);
#define LWIP_RAND() (lwip_rand())

#endif /* LWIP_ARCH_CC_H */
