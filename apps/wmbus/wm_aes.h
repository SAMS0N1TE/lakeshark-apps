/* AES-128 (FIPS-197) with the modes Wireless M-Bus needs: CBC decryption
 * (security modes 5 and 7), CTR (ELL encryption) and AES-CMAC (RFC 4493,
 * the mode 7 key derivation). Written for this app from the standards. */
#ifndef WM_AES_H
#define WM_AES_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t rk[176];            /* 11 round keys */
} wm_aes_t;

void wm_aes_init(wm_aes_t *a, const uint8_t key[16]);
void wm_aes_encrypt(const wm_aes_t *a, const uint8_t in[16], uint8_t out[16]);
void wm_aes_decrypt(const wm_aes_t *a, const uint8_t in[16], uint8_t out[16]);

/* In place; len is a multiple of 16. */
void wm_aes_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t *buf, size_t len);
/* In place, any length; the counter block is iv, incremented as a 128-bit
   big-endian number per block. */
void wm_aes_ctr(const uint8_t key[16], const uint8_t iv[16], uint8_t *buf, size_t len);
void wm_aes_cmac(const uint8_t key[16], const uint8_t *msg, size_t len, uint8_t mac[16]);

#endif
