/* shim: mbedtls/ssl_internal.h is gone in mbedTLS 3.x; lwIP altcp_tls_mbedtls.c only needs this one (still exported) private symbol. */
#include "mbedtls/ssl.h"
int mbedtls_ssl_flush_output(mbedtls_ssl_context *ssl);
