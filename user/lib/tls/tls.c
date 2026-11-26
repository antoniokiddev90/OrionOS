#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "orion_tls.h"
#include "tls_internal.h"

#define TLS_VERSION_MAJOR 3
#define TLS_VERSION_MINOR 3
#define TLS_RECORD_LIMIT 18432
#define TLS_HANDSHAKE_LIMIT 32768
#define TLS_TIMEOUT 3000
#define TLS_CIPHER_ECDHE_ECDSA_AES128_GCM_SHA256 0xC02B
#define TLS_CIPHER_ECDHE_RSA_AES128_GCM_SHA256 0xC02F

struct tls_state {
    int descriptor;
    int active;
    int extended_master_secret;
    uint8_t client_random[32];
    uint8_t server_random[32];
    uint8_t client_private[32];
    uint8_t server_public[32];
    uint8_t client_fixed_iv[4];
    uint8_t server_fixed_iv[4];
    struct tls_aes128 client_aes;
    struct tls_aes128 server_aes;
    uint64_t client_sequence;
    uint64_t server_sequence;
    struct tls_sha256 transcript;
    uint8_t record_data[TLS_RECORD_LIMIT];
    uint8_t transmit_data[TLS_RECORD_LIMIT + 5];
    uint8_t handshake_pending[TLS_HANDSHAKE_LIMIT];
    size_t handshake_pending_length;
    uint8_t handshake_message[TLS_HANDSHAKE_LIMIT];
    uint8_t application_data[TLS_RECORD_LIMIT];
    size_t application_length;
    size_t application_offset;
};

static struct tls_state tls;

static uint16_t tls_read_u16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t tls_read_u24(const uint8_t *data) {
    return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
}

static void tls_write_u16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void tls_write_u24(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value >> 16);
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)value;
}

static void tls_write_u64(uint8_t *data, uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        data[i] = (uint8_t)value;
        value >>= 8;
    }
}

static int tls_send_all(int descriptor, const uint8_t *data, size_t length) {
    size_t sent = 0;
    uint64_t last_activity = getticks();
    while (sent < length) {
        ssize_t amount = send(descriptor, data + sent, length - sent, 0);
        if (amount > 0) {
            sent += (size_t)amount;
            last_activity = getticks();
        } else if (getticks() - last_activity >= TLS_TIMEOUT) {
            return -1;
        } else {
            sched_yield();
        }
    }
    return 0;
}

static int tls_receive_exact(int descriptor, uint8_t *data, size_t length) {
    size_t received = 0;
    uint64_t last_activity = getticks();
    while (received < length) {
        ssize_t amount = recv(descriptor, data + received, length - received, 0);
        if (amount > 0) {
            received += (size_t)amount;
            last_activity = getticks();
        } else if (amount == 0 || getticks() - last_activity >= TLS_TIMEOUT) {
            return -1;
        } else {
            sched_yield();
        }
    }
    return 0;
}

static int tls_write_record(int descriptor, uint8_t type, const uint8_t *payload, size_t length) {
    if (length > TLS_RECORD_LIMIT) return -1;
    tls.transmit_data[0] = type;
    tls.transmit_data[1] = TLS_VERSION_MAJOR;
    tls.transmit_data[2] = TLS_VERSION_MINOR;
    tls_write_u16(tls.transmit_data + 3, (uint16_t)length);
    if (length) memcpy(tls.transmit_data + 5, payload, length);
    return tls_send_all(descriptor, tls.transmit_data, length + 5);
}

static int tls_read_record(int descriptor, uint8_t *type, size_t *length) {
    uint8_t header[5];
    if (tls_receive_exact(descriptor, header, sizeof(header)) != 0) return -1;
    if (header[1] != TLS_VERSION_MAJOR || header[2] != TLS_VERSION_MINOR) return -1;
    size_t record_length = tls_read_u16(header + 3);
    if (record_length > TLS_RECORD_LIMIT) return -1;
    if (tls_receive_exact(descriptor, tls.record_data, record_length) != 0) return -1;
    *type = header[0];
    *length = record_length;
    return 0;
}

static int tls_append_u8(uint8_t *data, size_t capacity, size_t *length, uint8_t value) {
    if (*length >= capacity) return -1;
    data[(*length)++] = value;
    return 0;
}

static int tls_append_data(uint8_t *data, size_t capacity, size_t *length, const void *source, size_t amount) {
    if (amount > capacity - *length) return -1;
    memcpy(data + *length, source, amount);
    *length += amount;
    return 0;
}

static int tls_append_extension(uint8_t *data, size_t capacity, size_t *length, uint16_t type, const uint8_t *value, size_t value_length) {
    if (value_length > 65535 || *length > capacity || capacity - *length < value_length + 4) return -1;
    tls_write_u16(data + *length, type);
    tls_write_u16(data + *length + 2, (uint16_t)value_length);
    if (value_length) memcpy(data + *length + 4, value, value_length);
    *length += value_length + 4;
    return 0;
}

static int tls_build_client_hello(const char *hostname, uint8_t *message, size_t capacity, size_t *message_length) {
    uint8_t body[1024];
    uint8_t extensions[768];
    uint8_t extension_data[512];
    size_t body_length = 0;
    size_t extension_length = 0;
    size_t extension_data_length = 0;
    size_t hostname_length = strlen(hostname);
    if (hostname_length > 240) return -1;
    if (tls_random_bytes(tls.client_random, sizeof(tls.client_random)) != 0 ||
        tls_random_bytes(tls.client_private, sizeof(tls.client_private)) != 0) return -1;
    if (tls_append_u8(body, sizeof(body), &body_length, TLS_VERSION_MAJOR) != 0 ||
        tls_append_u8(body, sizeof(body), &body_length, TLS_VERSION_MINOR) != 0 ||
        tls_append_data(body, sizeof(body), &body_length, tls.client_random, sizeof(tls.client_random)) != 0 ||
        tls_append_u8(body, sizeof(body), &body_length, 0) != 0) return -1;
    tls_write_u16(body + body_length, 4);
    body_length += 2;
    tls_write_u16(body + body_length, TLS_CIPHER_ECDHE_ECDSA_AES128_GCM_SHA256);
    tls_write_u16(body + body_length + 2, TLS_CIPHER_ECDHE_RSA_AES128_GCM_SHA256);
    body_length += 4;
    if (tls_append_u8(body, sizeof(body), &body_length, 1) != 0 ||
        tls_append_u8(body, sizeof(body), &body_length, 0) != 0) return -1;
    if (hostname_length) {
        int numeric_host = 1;
        for (size_t i = 0; i < hostname_length; ++i) {
            if (!((hostname[i] >= '0' && hostname[i] <= '9') || hostname[i] == '.')) numeric_host = 0;
        }
        if (!numeric_host) {
            tls_write_u16(extension_data, (uint16_t)(hostname_length + 3));
            extension_data[2] = 0;
            tls_write_u16(extension_data + 3, (uint16_t)hostname_length);
            memcpy(extension_data + 5, hostname, hostname_length);
            extension_data_length = hostname_length + 5;
            if (tls_append_extension(extensions, sizeof(extensions), &extension_length, 0, extension_data, extension_data_length) != 0) return -1;
        }
    }
    extension_data[0] = 0;
    extension_data[1] = 4;
    tls_write_u16(extension_data + 2, 29);
    tls_write_u16(extension_data + 4, 23);
    if (tls_append_extension(extensions, sizeof(extensions), &extension_length, 10, extension_data, 6) != 0) return -1;
    extension_data[0] = 1;
    extension_data[1] = 0;
    if (tls_append_extension(extensions, sizeof(extensions), &extension_length, 11, extension_data, 2) != 0) return -1;
    extension_data[0] = 0;
    extension_data[1] = 8;
    extension_data[2] = 4;
    extension_data[3] = 3;
    extension_data[4] = 8;
    extension_data[5] = 4;
    extension_data[6] = 4;
    extension_data[7] = 1;
    extension_data[8] = 5;
    extension_data[9] = 1;
    if (tls_append_extension(extensions, sizeof(extensions), &extension_length, 13, extension_data, 10) != 0) return -1;
    extension_data[0] = 0;
    if (tls_append_extension(extensions, sizeof(extensions), &extension_length, 0xFF01, extension_data, 1) != 0) return -1;
    if (tls_append_extension(extensions, sizeof(extensions), &extension_length, 23, NULL, 0) != 0) return -1;
    if (extension_length > 65535 || body_length + extension_length + 2 > sizeof(body)) return -1;
    tls_write_u16(body + body_length, (uint16_t)extension_length);
    body_length += 2;
    memcpy(body + body_length, extensions, extension_length);
    body_length += extension_length;
    if (body_length > 0xFFFFFF || body_length + 4 > capacity) return -1;
    message[0] = 1;
    tls_write_u24(message + 1, (uint32_t)body_length);
    memcpy(message + 4, body, body_length);
    *message_length = body_length + 4;
    return 0;
}

static int tls_send_handshake(int descriptor, const uint8_t *message, size_t message_length) {
    if (tls_write_record(descriptor, 22, message, message_length) != 0) return -1;
    tls_sha256_update(&tls.transcript, message, message_length);
    return 0;
}

static int tls_read_handshake(int descriptor, const uint8_t **message, size_t *message_length) {
    while (tls.handshake_pending_length < 4) {
        uint8_t type;
        size_t length;
        if (tls_read_record(descriptor, &type, &length) != 0 || type != 22 ||
            length > TLS_HANDSHAKE_LIMIT - tls.handshake_pending_length) return -1;
        memcpy(tls.handshake_pending + tls.handshake_pending_length, tls.record_data, length);
        tls.handshake_pending_length += length;
    }
    uint32_t body_length = tls_read_u24(tls.handshake_pending + 1);
    if (body_length > TLS_HANDSHAKE_LIMIT - 4) return -1;
    size_t total_length = (size_t)body_length + 4;
    while (tls.handshake_pending_length < total_length) {
        uint8_t type;
        size_t length;
        if (tls_read_record(descriptor, &type, &length) != 0 || type != 22 ||
            length > TLS_HANDSHAKE_LIMIT - tls.handshake_pending_length) return -1;
        memcpy(tls.handshake_pending + tls.handshake_pending_length, tls.record_data, length);
        tls.handshake_pending_length += length;
    }
    memcpy(tls.handshake_message, tls.handshake_pending, total_length);
    tls_sha256_update(&tls.transcript, tls.handshake_message, total_length);
    tls.handshake_pending_length -= total_length;
    for (size_t i = 0; i < tls.handshake_pending_length; ++i) {
        tls.handshake_pending[i] = tls.handshake_pending[total_length + i];
    }
    *message = tls.handshake_message;
    *message_length = total_length;
    return 0;
}

static int tls_parse_server_hello(const uint8_t *body, size_t length) {
    if (length < 38 || body[0] != TLS_VERSION_MAJOR || body[1] != TLS_VERSION_MINOR) return -1;
    memcpy(tls.server_random, body + 2, sizeof(tls.server_random));
    size_t offset = 34;
    size_t session_length = body[offset++];
    if (session_length > length - offset) return -1;
    offset += session_length;
    if (length - offset < 3) return -1;
    uint16_t cipher = tls_read_u16(body + offset);
    offset += 2;
    if (cipher != TLS_CIPHER_ECDHE_ECDSA_AES128_GCM_SHA256 && cipher != TLS_CIPHER_ECDHE_RSA_AES128_GCM_SHA256) return -1;
    if (body[offset++] != 0) return -1;
    if (offset == length) return 0;
    if (length - offset < 2) return -1;
    size_t extensions_length = tls_read_u16(body + offset);
    offset += 2;
    if (extensions_length != length - offset) return -1;
    size_t extensions_end = offset + extensions_length;
    while (offset < extensions_end) {
        if (extensions_end - offset < 4) return -1;
        uint16_t extension_type = tls_read_u16(body + offset);
        size_t extension_size = tls_read_u16(body + offset + 2);
        offset += 4;
        if (extension_size > extensions_end - offset) return -1;
        if (extension_type == 23) {
            if (extension_size != 0) return -1;
            tls.extended_master_secret = 1;
        }
        offset += extension_size;
    }
    return 0;
}

static int tls_parse_certificate(const uint8_t *body, size_t length) {
    if (length < 6 || tls_read_u24(body) != length - 3) return -1;
    size_t offset = 3;
    size_t count = 0;
    while (offset < length) {
        if (length - offset < 3) return -1;
        size_t certificate_length = tls_read_u24(body + offset);
        offset += 3;
        if (!certificate_length || certificate_length > length - offset) return -1;
        offset += certificate_length;
        ++count;
    }
    return count ? 0 : -1;
}

static int tls_parse_server_key_exchange(const uint8_t *body, size_t length) {
    if (length < 4 || body[0] != 3 || tls_read_u16(body + 1) != 29) return -1;
    size_t point_length = body[3];
    if (point_length != 32 || length < 4 + point_length + 4) return -1;
    memcpy(tls.server_public, body + 4, 32);
    size_t offset = 36;
    uint16_t signature_length = tls_read_u16(body + offset + 2);
    offset += 4;
    if (!signature_length || signature_length != length - offset) return -1;
    return 0;
}

static void tls_transcript_digest(uint8_t digest[32]) {
    struct tls_sha256 copy = tls.transcript;
    tls_sha256_final(&copy, digest);
}

static int tls_make_aad(uint64_t sequence, uint8_t type, size_t length, uint8_t aad[13]) {
    if (length > 65535) return -1;
    tls_write_u64(aad, sequence);
    aad[8] = type;
    aad[9] = TLS_VERSION_MAJOR;
    aad[10] = TLS_VERSION_MINOR;
    tls_write_u16(aad + 11, (uint16_t)length);
    return 0;
}

static int tls_encrypt_record(int descriptor, uint8_t type, const uint8_t *plaintext, size_t plaintext_length) {
    if (plaintext_length > 16384) return -1;
    uint8_t explicit_nonce[8];
    uint8_t aad[13];
    uint8_t tag[16];
    uint64_t sequence = tls.client_sequence;
    tls_write_u64(explicit_nonce, sequence);
    if (tls_make_aad(sequence, type, plaintext_length, aad) != 0) return -1;
    tls.transmit_data[0] = type;
    tls.transmit_data[1] = TLS_VERSION_MAJOR;
    tls.transmit_data[2] = TLS_VERSION_MINOR;
    tls_write_u16(tls.transmit_data + 3, (uint16_t)(plaintext_length + 24));
    memcpy(tls.transmit_data + 5, explicit_nonce, sizeof(explicit_nonce));
    if (tls_aes128_gcm_encrypt(&tls.client_aes, tls.client_fixed_iv, explicit_nonce, aad, sizeof(aad), plaintext, plaintext_length,
                               tls.transmit_data + 13, tag) != 1) return -1;
    memcpy(tls.transmit_data + 13 + plaintext_length, tag, sizeof(tag));
    if (tls_send_all(descriptor, tls.transmit_data, plaintext_length + 29) != 0) return -1;
    ++tls.client_sequence;
    return 0;
}

static int tls_decrypt_record(uint8_t type, size_t record_length, uint8_t *plaintext, size_t *plaintext_length) {
    if (record_length < 24) return -1;
    size_t length = record_length - 24;
    uint8_t aad[13];
    if (tls_make_aad(tls.server_sequence, type, length, aad) != 0) return -1;
    const uint8_t *explicit_nonce = tls.record_data;
    const uint8_t *ciphertext = tls.record_data + 8;
    const uint8_t *tag = ciphertext + length;
    if (tls_aes128_gcm_decrypt(&tls.server_aes, tls.server_fixed_iv, explicit_nonce, aad, sizeof(aad), ciphertext, length, tag, plaintext) != 1) return -1;
    ++tls.server_sequence;
    *plaintext_length = length;
    return 0;
}

static int tls_finish_handshake(int descriptor, const uint8_t *premaster, size_t premaster_length) {
    (void)descriptor;
    uint8_t seed[64];
    uint8_t master_secret[48];
    uint8_t key_block[40];
    uint8_t digest[32];
    uint8_t verify_data[12];
    uint8_t finished[16];
    uint8_t change_cipher_spec = 1;
    if (tls.extended_master_secret) {
        tls_transcript_digest(digest);
        tls_prf_sha256(premaster, premaster_length, "extended master secret", digest, sizeof(digest), master_secret, sizeof(master_secret));
    } else {
        memcpy(seed, tls.client_random, 32);
        memcpy(seed + 32, tls.server_random, 32);
        tls_prf_sha256(premaster, premaster_length, "master secret", seed, sizeof(seed), master_secret, sizeof(master_secret));
    }
    memcpy(seed, tls.server_random, 32);
    memcpy(seed + 32, tls.client_random, 32);
    tls_prf_sha256(master_secret, sizeof(master_secret), "key expansion", seed, sizeof(seed), key_block, sizeof(key_block));
    tls_aes128_init(&tls.client_aes, key_block);
    tls_aes128_init(&tls.server_aes, key_block + 16);
    memcpy(tls.client_fixed_iv, key_block + 32, 4);
    memcpy(tls.server_fixed_iv, key_block + 36, 4);
    tls.client_sequence = 0;
    tls.server_sequence = 0;
    if (tls_write_record(tls.descriptor, 20, &change_cipher_spec, 1) != 0) return -1;
    tls_transcript_digest(digest);
    tls_prf_sha256(master_secret, sizeof(master_secret), "client finished", digest, sizeof(digest), verify_data, sizeof(verify_data));
    finished[0] = 20;
    tls_write_u24(finished + 1, sizeof(verify_data));
    memcpy(finished + 4, verify_data, sizeof(verify_data));
    if (tls_encrypt_record(tls.descriptor, 22, finished, sizeof(finished)) != 0) return -1;
    tls_sha256_update(&tls.transcript, finished, sizeof(finished));
    uint8_t record_type;
    size_t record_length;
    if (tls_read_record(tls.descriptor, &record_type, &record_length) != 0 || record_type != 20 ||
        record_length != 1 || tls.record_data[0] != 1) return -1;
    if (tls_read_record(tls.descriptor, &record_type, &record_length) != 0 || record_type != 22) return -1;
    size_t plaintext_length;
    if (tls_decrypt_record(record_type, record_length, tls.application_data, &plaintext_length) != 0 || plaintext_length != 16 ||
        tls.application_data[0] != 20 || tls_read_u24(tls.application_data + 1) != 12) return -1;
    tls_transcript_digest(digest);
    tls_prf_sha256(master_secret, sizeof(master_secret), "server finished", digest, sizeof(digest), verify_data, sizeof(verify_data));
    uint8_t difference = 0;
    for (size_t i = 0; i < sizeof(verify_data); ++i) difference |= (uint8_t)(verify_data[i] ^ tls.application_data[4 + i]);
    if (difference) return -1;
    tls_sha256_update(&tls.transcript, tls.application_data, plaintext_length);
    tls.active = 1;
    return 0;
}

int orion_tls_connect(int descriptor, const char *hostname) {
    memset(&tls, 0, sizeof(tls));
    tls.descriptor = descriptor;
    tls_sha256_init(&tls.transcript);
    uint8_t hello[1028];
    size_t hello_length;
    if (tls_build_client_hello(hostname, hello, sizeof(hello), &hello_length) != 0 ||
        tls_send_handshake(descriptor, hello, hello_length) != 0) return -1;
    int certificate_seen = 0;
    int key_exchange_seen = 0;
    int server_hello_seen = 0;
    for (;;) {
        const uint8_t *message;
        size_t message_length;
        if (tls_read_handshake(descriptor, &message, &message_length) != 0) return -1;
        uint8_t type = message[0];
        const uint8_t *body = message + 4;
        size_t body_length = message_length - 4;
        if (type == 2) {
            if (server_hello_seen || tls_parse_server_hello(body, body_length) != 0) return -1;
            server_hello_seen = 1;
        } else if (type == 11) {
            if (!server_hello_seen || certificate_seen || tls_parse_certificate(body, body_length) != 0) return -1;
            certificate_seen = 1;
        } else if (type == 12) {
            if (!certificate_seen || key_exchange_seen || tls_parse_server_key_exchange(body, body_length) != 0) return -1;
            key_exchange_seen = 1;
        } else if (type == 14) {
            if (body_length || !certificate_seen || !key_exchange_seen) return -1;
            break;
        } else if (type == 13) {
            return -1;
        }
    }
    uint8_t client_public[32];
    uint8_t premaster[32];
    if (tls_x25519(client_public, tls.client_private, (const uint8_t[32]){9}) != 0 ||
        tls_x25519(premaster, tls.client_private, tls.server_public) != 0) return -1;
    uint8_t key_exchange[37];
    key_exchange[0] = 16;
    tls_write_u24(key_exchange + 1, 33);
    key_exchange[4] = 32;
    memcpy(key_exchange + 5, client_public, sizeof(client_public));
    if (tls_send_handshake(descriptor, key_exchange, sizeof(key_exchange)) != 0) return -1;
    if (tls_finish_handshake(descriptor, premaster, sizeof(premaster)) != 0) return -1;
    return 0;
}

long orion_tls_send(int descriptor, const void *buffer, size_t length) {
    if (!tls.active || descriptor != tls.descriptor || (length && !buffer)) return -1;
    const uint8_t *data = (const uint8_t *)buffer;
    size_t sent = 0;
    while (sent < length) {
        size_t amount = length - sent;
        if (amount > 16384) amount = 16384;
        if (tls_encrypt_record(descriptor, 23, data + sent, amount) != 0) return -1;
        sent += amount;
    }
    return (long)sent;
}

long orion_tls_recv(int descriptor, void *buffer, size_t capacity) {
    if (!tls.active || descriptor != tls.descriptor || (capacity && !buffer)) return -1;
    if (!capacity) return 0;
    for (;;) {
        if (tls.application_offset < tls.application_length) {
            size_t amount = tls.application_length - tls.application_offset;
            if (amount > capacity) amount = capacity;
            memcpy(buffer, tls.application_data + tls.application_offset, amount);
            tls.application_offset += amount;
            if (tls.application_offset == tls.application_length) {
                tls.application_offset = 0;
                tls.application_length = 0;
            }
            return (long)amount;
        }
        uint8_t type;
        size_t record_length;
        if (tls_read_record(descriptor, &type, &record_length) != 0) return -1;
        if (type != 23 && type != 21) return -1;
        size_t plaintext_length;
        if (tls_decrypt_record(type, record_length, tls.application_data, &plaintext_length) != 0) return -1;
        if (type == 21) {
            if (plaintext_length != 2) return -1;
            if (tls.application_data[1] == 0) return 0;
            return -1;
        }
        tls.application_length = plaintext_length;
        tls.application_offset = 0;
        if (!plaintext_length) continue;
    }
}

void orion_tls_close(int descriptor) {
    if (tls.descriptor == descriptor) memset(&tls, 0, sizeof(tls));
}
