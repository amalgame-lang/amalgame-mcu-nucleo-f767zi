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
/* CN du sujet sans mbedtls_x509_dn_gets (retire par MBEDTLS_X509_REMOVE_INFO) : on parcourt le DN. */
static void pki_subject_cn(const mbedtls_x509_crt *crt, char *out, unsigned cap) {
    out[0] = 0;
    for (const mbedtls_x509_name *n = &crt->subject; n; n = n->next)
        if (n->oid.len == 3 && memcmp(n->oid.p, "\x55\x04\x03", 3) == 0) {   /* OID 2.5.4.3 = commonName */
            unsigned l = n->val.len < cap - 1u ? (unsigned) n->val.len : cap - 1u;
            memcpy(out, n->val.p, l); out[l] = 0; return;
        }
}
int mcu_pki_set_cert(const char *pem) {
    ensure_init();
    if (!pem || strlen(pem) >= sizeof g_certpem) return -1;
    /* Parse dans un contexte TEMPORAIRE, remplace seulement si tout est bon : un certificat refuse ne doit
     * PAS detruire celui deja en place (mesure au banc 2026-09-05 : une ligne `cert` erronee mettait cert=0). */
    mbedtls_x509_crt tmp; mbedtls_x509_crt_init(&tmp);
    if (mbedtls_x509_crt_parse(&tmp, (const unsigned char *) pem, strlen(pem) + 1) != 0) { mbedtls_x509_crt_free(&tmp); return -2; }
    /* the cert's public key must match our private key (defends against a wrong cert) */
    if (g_key && mbedtls_pk_check_pair(&tmp.pk, &g_pk, pki_rng, 0) != 0) { mbedtls_x509_crt_free(&tmp); return -4; }
    mbedtls_x509_crt_free(&g_crt); g_crt = tmp;   /* transfert de propriete : tmp ne doit plus etre libere */
    strcpy(g_certpem, pem); g_cert = 1;
    pki_subject_cn(&g_crt, g_subj, sizeof g_subj);
    return 0;
}
const char *mcu_pki_cert_expiry(void) {          /* notAfter "AAAA-MM-JJ" (rotation : renouveler a < 30 j) */
    static char b[12]; b[0] = 0;
    if (!g_cert) return b;
    const mbedtls_x509_time *t = &g_crt.valid_to;
    b[0] = (char)('0' + t->year / 1000 % 10); b[1] = (char)('0' + t->year / 100 % 10);
    b[2] = (char)('0' + t->year / 10 % 10);   b[3] = (char)('0' + t->year % 10); b[4] = '-';
    b[5] = (char)('0' + t->mon / 10);  b[6] = (char)('0' + t->mon % 10);  b[7] = '-';
    b[8] = (char)('0' + t->day / 10);  b[9] = (char)('0' + t->day % 10);  b[10] = 0;
    return b;
}
int mcu_pki_have_cert(void) { return g_cert; }
const char *mcu_pki_cert_pem(void) { return g_cert ? g_certpem : ""; }
const char *mcu_pki_cert_subject(void) { return g_cert ? g_subj : ""; }
void *mcu_pki_cert_ctx(void) { return g_cert ? (void *) &g_crt : 0; }
void *mcu_pki_pk_ctx(void) { return g_key ? (void *) &g_pk : 0; }
