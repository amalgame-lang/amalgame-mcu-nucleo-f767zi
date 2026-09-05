#include <string.h>
#include "mbedtls/pk.h"
#include "mbedtls/ecp.h"
#include "mbedtls/x509_crt.h"
#include "mcu_pki.h"

extern int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);
static int pki_rng(void *ctx, unsigned char *out, size_t len) { (void) ctx; size_t o; return mbedtls_hardware_poll(0, out, len, &o); }

static mbedtls_pk_context g_pk; static int g_key;
static mbedtls_x509_crt  g_crt; static int g_cert;
static char g_certpem[1200]; static char g_subj[64];
static int g_init;
static void ensure_init(void) { if (!g_init) { mbedtls_pk_init(&g_pk); mbedtls_x509_crt_init(&g_crt); g_init = 1; } }

int mcu_pki_generate(void) {
    ensure_init(); mbedtls_pk_free(&g_pk); mbedtls_pk_init(&g_pk); g_key = 0;
    if (mbedtls_pk_setup(&g_pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0) return -1;
    if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(g_pk), pki_rng, 0) != 0) return -2;
    g_key = 1; return 0;
}
int mcu_pki_load_key_pem(const char *pem) {
    ensure_init(); mbedtls_pk_free(&g_pk); mbedtls_pk_init(&g_pk); g_key = 0;
    if (mbedtls_pk_parse_key(&g_pk, (const unsigned char *) pem, strlen(pem) + 1, 0, 0, pki_rng, 0) != 0) return -1;
    g_key = 1; return 0;
}
int mcu_pki_have_key(void) { return g_key; }

const char *mcu_pki_pubkey_pem(void) {
    static char buf[300]; buf[0] = 0;
    if (!g_key) return buf;
    if (mbedtls_pk_write_pubkey_pem(&g_pk, (unsigned char *) buf, sizeof buf) != 0) { buf[0] = 0; }
    return buf;
}
const char *mcu_pki_key_pem(void) {
    static char buf[400]; buf[0] = 0;
    if (!g_key) return buf;
    if (mbedtls_pk_write_key_pem(&g_pk, (unsigned char *) buf, sizeof buf) != 0) { buf[0] = 0; }
    return buf;
}
int mcu_pki_set_cert(const char *pem) {
    ensure_init();
    if (!pem || strlen(pem) >= sizeof g_certpem) return -1;
    mbedtls_x509_crt_free(&g_crt); mbedtls_x509_crt_init(&g_crt); g_cert = 0;
    if (mbedtls_x509_crt_parse(&g_crt, (const unsigned char *) pem, strlen(pem) + 1) != 0) return -2;
    /* the cert's public key must match our private key (defends against a wrong cert) */
    if (g_key && mbedtls_pk_check_pair(&g_crt.pk, &g_pk, pki_rng, 0) != 0) return -4;
    strcpy(g_certpem, pem); g_cert = 1;
    /* subject CN for status */
    g_subj[0] = 0; mbedtls_x509_dn_gets(g_subj, sizeof g_subj, &g_crt.subject);
    return 0;
}
int mcu_pki_have_cert(void) { return g_cert; }
const char *mcu_pki_cert_pem(void) { return g_cert ? g_certpem : ""; }
const char *mcu_pki_cert_subject(void) { return g_cert ? g_subj : ""; }
void *mcu_pki_cert_ctx(void) { return g_cert ? (void *) &g_crt : 0; }
void *mcu_pki_pk_ctx(void) { return g_key ? (void *) &g_pk : 0; }
