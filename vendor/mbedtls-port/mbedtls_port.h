/* mbedTLS platform port for the STM32F767 board package (see mcu_mbedtls_config.h). */
#ifndef MBEDTLS_PORT_H
#define MBEDTLS_PORT_H
#include <stddef.h>
#include <time.h>
time_t mcu_tls_time(time_t *t);   /* UNIX seconds from the wall clock, 0 while unknown */
void  *mcu_tls_calloc(size_t n, size_t size);
void   mcu_tls_free(void *p);
int    mcu_tls_snprintf(char *s, size_t n, const char *fmt, ...);
int    mcu_tls_printf(const char *fmt, ...);
int    mcu_tls_fprintf(void *stream, const char *fmt, ...);
void   mcu_tls_exit(int status);
int    mcu_tls_setbuf(void *stream, char *buf);
/* pool statistics for sizing: bytes in use now, high-water mark, failed allocations */
size_t mcu_tls_pool_used(void);
size_t mcu_tls_pool_hwm(void);
unsigned mcu_tls_pool_failures(void);
/* trusted roots (PEM, NUL-terminated): ISRG Root X1 + X2 */
extern const char mcu_tls_roots_pem[];
extern const size_t mcu_tls_roots_pem_len;   /* includes the NUL */
#endif
