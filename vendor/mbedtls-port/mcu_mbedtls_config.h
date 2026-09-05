/* mbedTLS 3.6 configuration for the MusiCall box (STM32F767, freestanding, lwIP altcp_tls).
 * TLS 1.2 CLIENT only: ECDHE-ECDSA / ECDHE-RSA + AES-GCM + SHA-256/384, X.509 chain verification
 * against ISRG Root X1 (RSA-4096, cross-signs the Let's Encrypt E intermediates) and X2 (P-384).
 * No filesystem, no clock (certificate validity dates are NOT checked until NTP lands — see
 * docs), no std printf/exit, entropy = STM32 RNG peripheral, memory = static pool (mbedtls_port.c).
 * Records capped at 4 KB via the max_fragment_length extension (OpenSSL servers honour it). */
#ifndef MBEDTLS_PORT_CONFIG_H
#define MBEDTLS_PORT_CONFIG_H

/* platform — the *_MACRO functions below are declared in mbedtls_port.h */
#include "mbedtls_port.h"
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_CALLOC_MACRO    mcu_tls_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO      mcu_tls_free
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO  mcu_tls_snprintf
#define MBEDTLS_PLATFORM_PRINTF_MACRO    mcu_tls_printf
#define MBEDTLS_PLATFORM_FPRINTF_MACRO   mcu_tls_fprintf
#define MBEDTLS_PLATFORM_EXIT_MACRO      mcu_tls_exit
#define MBEDTLS_PLATFORM_SETBUF_MACRO    mcu_tls_setbuf
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_MAX_SOURCES 2
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

/* crypto */
#define MBEDTLS_AES_C
#define MBEDTLS_AES_ROM_TABLES        /* AES = fallback only (ChaCha20 preferred); tables in flash, fine with the I-cache */
#define MBEDTLS_AES_FEWER_TABLES
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CHACHAPOLY_C          /* software-friendly AEAD: preferred over AES-GCM on the M7 (no AES hardware) */
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA256_SMALLER
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_SHA512_SMALLER
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_MPI_MAX_SIZE 512            /* RSA-4096 (ISRG Root X1) */
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECP_WINDOW_SIZE 2
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 0
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C

/* X.509 */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_REMOVE_INFO

/* TLS 1.2 client */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET   /* RFC 7627; OpenSSL 3 servers only resume EMS sessions (measured 2026-09-05) */
#define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
#define MBEDTLS_SSL_IN_CONTENT_LEN  4096
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096
#define MBEDTLS_SSL_MAX_CONTENT_LEN 4096   /* legacy name still read by lwIP's altcp_tls glue */
#define MBEDTLS_SSL_CIPHERSUITES                        \
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256, \
    MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,   \
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,    \
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,    \
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256

#endif
