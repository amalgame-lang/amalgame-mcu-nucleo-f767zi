/* On-target self-test of the mbedTLS port: entropy (STM32 RNG) → DRBG → parse the embedded roots →
 * client config → ssl context → one handshake step on a dummy bio (must return WANT_READ after the
 * ClientHello was produced). Reports pool usage. Shared by linkproof.c and the firmware probe. */
#include <string.h>
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls_port.h"

static int bio_send(void *ctx, const unsigned char *b, size_t n) { (void) ctx; (void) b; return (int) n; }
static int bio_recv(void *ctx, unsigned char *b, size_t n) { (void) ctx; (void) b; (void) n; return MBEDTLS_ERR_SSL_WANT_READ; }

/* step: which stage failed (0 = none, handshake reached). Returns the mbedTLS rc of that stage. */
int mcu_tls_selftest(int *step, unsigned *pool_hwm, unsigned *pool_fail, unsigned *pool_after) {
    static mbedtls_entropy_context entropy; static mbedtls_ctr_drbg_context drbg;
    static mbedtls_x509_crt roots; static mbedtls_ssl_config conf; static mbedtls_ssl_context ssl;
    int rc; *step = 0;
    mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&drbg);
    mbedtls_x509_crt_init(&roots); mbedtls_ssl_config_init(&conf); mbedtls_ssl_init(&ssl);
    *step = 1; rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char *) "musicall", 8);
    if (!rc) { *step = 2; rc = mbedtls_x509_crt_parse(&roots, (const unsigned char *) mcu_tls_roots_pem, mcu_tls_roots_pem_len); }
    if (!rc) { *step = 3; rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT); }
    if (!rc) {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&conf, &roots, NULL);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
        mbedtls_ssl_conf_max_frag_len(&conf, MBEDTLS_SSL_MAX_FRAG_LEN_4096);
        *step = 4; rc = mbedtls_ssl_setup(&ssl, &conf);
    }
    if (!rc) { *step = 5; rc = mbedtls_ssl_set_hostname(&ssl, "ctrl.musicall.network"); }
    if (!rc) { mbedtls_ssl_set_bio(&ssl, NULL, bio_send, bio_recv, NULL); *step = 6; rc = mbedtls_ssl_handshake(&ssl); if (rc == MBEDTLS_ERR_SSL_WANT_READ) { rc = 0; *step = 0; } }
    *pool_hwm = (unsigned) mcu_tls_pool_hwm(); *pool_fail = mcu_tls_pool_failures();
    mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&roots);
    mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&entropy);
    *pool_after = (unsigned) mcu_tls_pool_used();   /* 0 expected: everything freed */
    return rc;
}
