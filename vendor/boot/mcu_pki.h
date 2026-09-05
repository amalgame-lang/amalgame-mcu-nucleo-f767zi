/* mcu_pki — device identity for mTLS (docs/PKI.md étape 3). A P-256 keypair generated on-chip (private key never
 * leaves), the public key exported as PEM for enrolment, and the issued certificate stored. The application persists
 * the private-key PEM and the certificate PEM (flash_store) and reloads them at boot. Requires mbedTLS with
 * PK/PK_WRITE/ECP/ECDSA/X509_CRT_PARSE (mcu_mbedtls_config.h). */
#ifndef MCU_PKI_H
#define MCU_PKI_H
int         mcu_pki_generate(void);            /* fresh P-256 keypair into the in-RAM context; 0 ok */
int         mcu_pki_load_key_pem(const char *pem);  /* reload a stored private-key PEM; 0 ok */
int         mcu_pki_have_key(void);            /* 1 if a private key is loaded/generated */
const char *mcu_pki_pubkey_pem(void);          /* public key as PEM ("" on error) */
const char *mcu_pki_key_pem(void);             /* private key as PEM (to persist; "" on error) */
int         mcu_pki_set_cert(const char *pem); /* store+parse the issued certificate; 0 ok, verifies it matches our key */
int         mcu_pki_have_cert(void);           /* 1 if a valid certificate is loaded */
const char *mcu_pki_cert_pem(void);            /* the stored certificate PEM ("" if none) */
const char *mcu_pki_cert_subject(void);        /* subject CN of the cert, for `pki` status ("" if none) */
/* for the TLS client: the parsed cert chain + pk, to pass to mbedtls_ssl_conf_own_cert (NULL if none) */
void       *mcu_pki_cert_ctx(void);
void       *mcu_pki_pk_ctx(void);
#endif
