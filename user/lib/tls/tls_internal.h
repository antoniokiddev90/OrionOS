#ifndef ORION_TLS_INTERNAL_H
#define ORION_TLS_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

struct tls_sha256 {
    uint32_t state[8];
    uint64_t total;
    uint8_t block[64];
    size_t used;
};

struct tls_aes128 {
    uint8_t round_keys[176];
};

void tls_sha256_init(struct tls_sha256 *context);
void tls_sha256_update(struct tls_sha256 *context, const void *data, size_t length);
void tls_sha256_final(struct tls_sha256 *context, uint8_t digest[32]);
void tls_hmac_sha256(const uint8_t *key, size_t key_length, const uint8_t *data, size_t data_length, uint8_t digest[32]);
void tls_prf_sha256(const uint8_t *secret, size_t secret_length, const char *label, const uint8_t *seed, size_t seed_length, uint8_t *output, size_t output_length);
void tls_aes128_init(struct tls_aes128 *context, const uint8_t key[16]);
void tls_aes128_encrypt(const struct tls_aes128 *context, const uint8_t input[16], uint8_t output[16]);
int tls_aes128_gcm_encrypt(const struct tls_aes128 *context, const uint8_t fixed_iv[4], const uint8_t explicit_nonce[8], const uint8_t *aad, size_t aad_length, const uint8_t *input, size_t input_length, uint8_t *output, uint8_t tag[16]);
int tls_aes128_gcm_decrypt(const struct tls_aes128 *context, const uint8_t fixed_iv[4], const uint8_t explicit_nonce[8], const uint8_t *aad, size_t aad_length, const uint8_t *input, size_t input_length, const uint8_t tag[16], uint8_t *output);
int tls_x25519(uint8_t output[32], const uint8_t scalar[32], const uint8_t point[32]);
int tls_random_bytes(uint8_t *output, size_t length);

#endif
