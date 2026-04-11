#include <kernel/drivers/network/wifi_data_plane.h>
#include <kernel/kernel_string.h>
#include <kernel/kernel.h>
#include <stddef.h>

/* WPA2 constants */
#define WPA2_KEY_VERSION_1  1
#define EAPOL_VERSION       2
#define EAPOL_TYPE_KEY      3

#define EAPOL_KEY_INFO_TYPE_MASK        0x0007u
#define EAPOL_KEY_INFO_INSTALL          0x0040u
#define EAPOL_KEY_INFO_ACK              0x0080u
#define EAPOL_KEY_INFO_MIC              0x0100u
#define EAPOL_KEY_INFO_SECURE           0x0200u
#define EAPOL_KEY_INFO_ERROR            0x0400u
#define EAPOL_KEY_INFO_REQUEST          0x0800u
#define EAPOL_KEY_INFO_ENCRYPTED        0x1000u
#define EAPOL_KEY_INFO_SMK_MESSAGE      0x2000u

/* PTK (Pairwise Transient Key) derivation state */
struct wpa2_ptk {
    uint8_t kck[16];      /* Key Confirmation Key */
    uint8_t kek[16];      /* Key Encryption Key */
    uint8_t tk[16];       /* Temporal Key */
};

/* EAPOL-Key frame structure */
struct eapol_key {
    uint8_t version;
    uint8_t type;
    uint16_t length;
    uint8_t key_info_version;
    uint16_t key_length;
    uint64_t replay_counter;
    uint8_t key_nonce[32];
    uint8_t key_iv[16];
    uint8_t key_rsc[8];
    uint8_t key_id[8];
    uint8_t key_mic[16];
    uint16_t key_data_length;
    uint8_t key_data[256];
};

/* WPA2 handshake state */
struct wpa2_handshake {
    int step;  /* 0: idle, 1: awaiting msg1, 2: awaiting msg3, 3: complete */
    uint8_t pmk[32];  /* Pre-shared Master Key from passphrase */
    struct wpa2_ptk ptk;
    uint8_t anonce[32];  /* Authenticator nonce */
    uint8_t snonce[32];  /* Supplicant nonce */
    uint64_t replay_counter;
    uint32_t timeout;
};

/* Global handshake state */
static struct wpa2_handshake g_handshake;

/* Simple PBKDF2-SHA256 for PMK derivation (stub - real implementation needed) */
static void wpa2_derive_pmk(const uint8_t *passphrase,
                            int pass_len,
                            const uint8_t *ssid,
                            int ssid_len,
                            uint8_t *pmk) {
    /* Simplified: hash passphrase + SSID iteratively */
    /* Real implementation would use PBKDF2 with 4096 iterations */
    memset(pmk, 0, 32);

    for (int i = 0; i < 32; ++i) {
        pmk[i] = (0xCCu ^ passphrase[i % pass_len]) +
                 (0x55u ^ ssid[i % ssid_len]);
    }
}

/* PRF-256 for key derivation */
static void wpa2_kdf(const uint8_t *key,
                     int key_len,
                     const uint8_t *label,
                     const uint8_t *a,
                     const uint8_t *b,
                     int output_len,
                     uint8_t *output) {
    /* Simplified: just mix inputs together */
    /* Real implementation would use HMAC-SHA256 iteratively */
    memset(output, 0, output_len);

    for (int i = 0; i < key_len && i < output_len; ++i) {
        output[i] = key[i] ^ label[i % 16];
    }

    for (int i = 0; i < output_len; ++i) {
        if (a != 0) {
            output[i] ^= a[i % 32];
        }
        if (b != 0) {
            output[i] ^= b[i % 32];
        }
    }
}

/* Derive PTK from PMK */
static void wpa2_derive_ptk(const uint8_t *pmk,
                            const uint8_t *ap_mac,
                            const uint8_t *sta_mac,
                            const uint8_t *anonce,
                            const uint8_t *snonce,
                            struct wpa2_ptk *ptk) {
    uint8_t context[32];
    uint8_t label[] = "Pairwise key expansion";

    /* Build context: min(ap_mac, sta_mac) || max(ap_mac, sta_mac) || min(anonce, snonce) || max(anonce, snonce) */
    memcpy(&context[0], ap_mac, 6);
    memcpy(&context[6], sta_mac, 6);
    memcpy(&context[12], anonce, 10);
    memcpy(&context[22], snonce, 10);

    /* KDF to derive PTK components */
    wpa2_kdf(pmk, 32, label, anonce, snonce, 48, (uint8_t *)ptk);
}

/* Generate EAPOL-Key frame response (reserved for future WPA2 TX) */
__attribute__((unused))
static uint16_t wpa2_build_eapol_response(uint8_t *frame,
                                          const struct wpa2_handshake *hs,
                                          int msg_num) {
    struct eapol_key *key = (struct eapol_key *)frame;

    memset(key, 0, sizeof(struct eapol_key));

    key->version = EAPOL_VERSION;
    key->type = EAPOL_TYPE_KEY;
    key->length = sizeof(struct eapol_key) - 4;
    key->key_info_version = (WPA2_KEY_VERSION_1 & 0x07u);
    key->key_length = 16;  /* AES-CCMP */

    key->replay_counter = hs->replay_counter & 0xFFu;

    /* Set appropriate flags based on message */
    uint16_t key_info = 0;
    if (msg_num == 2) {
        key_info = EAPOL_KEY_INFO_TYPE_MASK | EAPOL_KEY_INFO_MIC;
        memcpy(key->key_nonce, hs->snonce, 32);
    } else if (msg_num == 4) {
        key_info = EAPOL_KEY_INFO_SECURE | EAPOL_KEY_INFO_MIC;
    }

    key->key_info_version |= (key_info & 0xFF00u) >> 8;
    key->length = (key_info & 0xFF);

    return sizeof(struct eapol_key);
}

/* Process received EAPOL-Key frame */
static int wpa2_process_eapol_key(const uint8_t *frame,
                                  uint16_t frame_len,
                                  struct wpa2_handshake *hs,
                                  __attribute__((unused)) struct wifi_device *dev) {
    const struct eapol_key *key = (const struct eapol_key *)frame;

    if (frame_len < 95) {  /* Minimum EAPOL-Key size */
        return -1;
    }

    /* Check replay counter */
    if ((key->replay_counter & 0xFFu) <= (hs->replay_counter & 0xFFu)) {
        return -1;
    }
    hs->replay_counter = key->replay_counter & 0xFFu;

    /* Extract nonce from frame */
    if (hs->step == 0) {
        /* Message 1: extract ANonce */
        memcpy(hs->anonce, key->key_nonce, 32);
        hs->step = 1;
        return 1;  /* Need response */
    } else if (hs->step == 2) {
        /* Message 3: verify and extract GTK */
        hs->step = 3;
        return 0;  /* Handshake complete */
    }

    return -1;
}

/* Start WPA2 handshake */
int wpa2_handshake_start(struct wifi_device *dev,
                         const struct wifi_credentials *creds) {
    if (!dev || !creds) {
        return -1;
    }

    /* Generate random nonce (simplified) */
    for (int i = 0; i < 32; ++i) {
        g_handshake.snonce[i] = (uint8_t)(0xABu + i);
    }

    /* Derive PMK from passphrase */
    wpa2_derive_pmk((const uint8_t *)creds->passphrase,
                    strlen((const char *)creds->passphrase),
                    (const uint8_t *)creds->ssid,
                    strlen((const char *)creds->ssid),
                    g_handshake.pmk);

    g_handshake.step = 0;
    g_handshake.replay_counter = 0;
    g_handshake.timeout = 0;

    dev->connection_state = WIFI_STATE_ASSOCIATING;
    return 0;
}

/* Continue handshake with received frame */
int wpa2_handshake_continue(struct wifi_device *dev, const uint8_t *frame, uint16_t len) {
    int result;

    if (!dev || !frame) {
        return -1;
    }

    result = wpa2_process_eapol_key(frame, len, &g_handshake, dev);

    if (result == 1) {
        /* Need to send response */
        uint8_t response[128];  /* Smaller buffer for simple response */
        uint16_t response_len = 64;  /* Simplified response size */

        /* Placeholder: actual EAPOL-Key frame building omitted */
        if (wifi_device_send_frame(dev, response, response_len) != 0) {
            return -1;
        }

        g_handshake.step++;
    }

    if (g_handshake.step >= 3) {
        /* Derive PTK */
        wpa2_derive_ptk(g_handshake.pmk,
                        dev->current_ap.bssid.octets,
                        dev->mac_addr,
                        g_handshake.anonce,
                        g_handshake.snonce,
                        &g_handshake.ptk);

        dev->connection_state = WIFI_STATE_CONNECTED;
        return 0;
    }

    return 0;
}

/* Get current handshake state */
int wpa2_handshake_get_state(void) {
    return g_handshake.step;
}

/* Get PTK for current connection */
const struct wpa2_ptk *wpa2_get_ptk(void) {
    if (g_handshake.step >= 3) {
        return &g_handshake.ptk;
    }
    return 0;
}
