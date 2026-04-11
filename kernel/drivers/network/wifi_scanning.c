#include <kernel/drivers/network/wifi_data_plane.h>
#include <kernel/kernel_string.h>
#include <kernel/kernel.h>
#include <stddef.h>
#include <string.h>

/* 802.11 Frame control values */
#define FC_VERSION_MASK     0x0003u
#define FC_TYPE_MASK        0x000Cu
#define FC_SUBTYPE_MASK     0x00F0u
#define FC_TODS             0x0100u
#define FC_FROMDS           0x0200u
#define FC_MOREFRAG         0x0400u
#define FC_RETRY            0x0800u
#define FC_PWR_MGT           0x1000u
#define FC_MORE_DATA        0x2000u
#define FC_PROTECTED        0x4000u
#define FC_ORDER            0x8000u

#define TYPE_DIRECTION_MGMT (0u << 2)
#define TYPE_DIRECTION_CTRL (1u << 2)
#define TYPE_DIRECTION_DATA (2u << 2)

#define SUBTYPE_PROBE_REQ   (0x04u << 4)
#define SUBTYPE_BEACON      (0x08u << 4)

/* Wi-Fi channels (2.4 GHz band for simplicity) */
#define CHANNEL_1_FREQ   2412u
#define CHANNEL_6_FREQ   2437u
#define CHANNEL_11_FREQ  2462u

/* 802.11 MAC frame header */
struct wifi_mac_header {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t addr1[6];  /* Destination */
    uint8_t addr2[6];  /* Source */
    uint8_t addr3[6];  /* BSSID */
    uint16_t seq_ctrl;
    /* Optional addr4 for 4-address frames */
};

/* Information elements */
struct wifi_ie_header {
    uint8_t type;
    uint8_t length;
    uint8_t data[0];
};

#define IE_TYPE_SSID        0u
#define IE_TYPE_SUPP_RATES  1u
#define IE_TYPE_DS_PARAM    3u
#define IE_TYPE_RSN         48u
#define IE_TYPE_EXT_SUPP    127u

/* Probe request frame structure */
struct wifi_probe_req {
    struct wifi_mac_header header;
    /* Fixed parameters (none for probe request) */
    /* Information elements follow */
};

/* Beacon frame structure */
struct wifi_beacon {
    struct wifi_mac_header header;
    uint64_t timestamp;
    uint16_t beacon_interval;
    uint16_t capabilities;
    /* Information elements follow */
};

/* Scan state (reserved for future use) */
/* static struct {
    uint8_t *probe_frames[14];
    uint16_t probe_sizes[14];
    uint32_t current_channel;
    uint32_t scan_progress;
} g_scan_state; */

/* Build 802.11 MAC header */
static void wifi_build_mac_header(struct wifi_mac_header *hdr,
                                  uint16_t frame_type,
                                  const uint8_t *addr1,
                                  const uint8_t *addr2,
                                  const uint8_t *addr3) {
    hdr->frame_control = frame_type;
    hdr->duration = 0;
    memcpy(hdr->addr1, addr1, 6);
    memcpy(hdr->addr2, addr2, 6);
    memcpy(hdr->addr3, addr3, 6);
    hdr->seq_ctrl = 0;
}

/* Build SSID information element */
static uint16_t wifi_build_ssid_ie(uint8_t *buffer, const char *ssid) {
    uint16_t len = (ssid != 0) ? strlen((const char *)ssid) : 0;
    if (len > 32) {
        len = 32;
    }

    buffer[0] = IE_TYPE_SSID;
    buffer[1] = len;
    if (len > 0) {
        memcpy(&buffer[2], ssid, len);
    }

    return 2 + len;
}

/* Build supported rates IE */
static uint16_t wifi_build_rates_ie(uint8_t *buffer) {
    /* 1, 2, 5.5, 11 Mbps rates */
    uint8_t rates[] = { 0x82u, 0x84u, 0x8Bu, 0x96u };
    buffer[0] = IE_TYPE_SUPP_RATES;
    buffer[1] = 4;
    memcpy(&buffer[2], rates, 4);
    return 6;
}

/* Build DS parameter set IE (reserved for future 802.11 TX) */
__attribute__((unused))
static uint16_t wifi_build_ds_ie(uint8_t *buffer, uint8_t channel) {
    buffer[0] = IE_TYPE_DS_PARAM;
    buffer[1] = 1;
    buffer[2] = channel;
    return 3;
}

/* Build RSN (WPA2) information element (reserved for future 802.11 TX) */
__attribute__((unused))
static uint16_t wifi_build_rsn_ie(uint8_t *buffer) {
    /* Minimal RSN IE for WPA2 support detection */
    uint8_t rsn_data[] = {
        0x01u, 0x00u,              /* RSN version 1 */
        0xCCu, 0x00u,              /* Group cipher: CCMP */
        0x01u, 0x00u,              /* Pairwise cipher count: 1 */
        0xCCu, 0x00u,              /* Pairwise cipher: CCMP */
        0x01u, 0x00u,              /* AKM count: 1 */
        0x02u, 0x00u               /* AKM: PSK */
    };

    buffer[0] = IE_TYPE_RSN;
    buffer[1] = sizeof(rsn_data);
    memcpy(&buffer[2], rsn_data, sizeof(rsn_data));

    return 2 + sizeof(rsn_data);
}

/* Build probe request frame (reserved for future 802.11 TX) */
__attribute__((unused))
static uint16_t wifi_build_probe_req(uint8_t *frame, const uint8_t *src_mac, const char *ssid) {
    uint16_t offset = 0;
    struct wifi_mac_header *hdr = (struct wifi_mac_header *)frame;
    uint8_t broadcast_addr[] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu };

    /* MAC header */
    wifi_build_mac_header(hdr,
                          TYPE_DIRECTION_MGMT | SUBTYPE_PROBE_REQ,
                          broadcast_addr,
                          src_mac,
                          broadcast_addr);
    offset += sizeof(struct wifi_mac_header);

    /* Information elements */
    offset += wifi_build_ssid_ie(&frame[offset], ssid);
    offset += wifi_build_rates_ie(&frame[offset]);

    return offset;
}

/* Extract SSID and security from beacon/probe response (reserved for future 802.11 RX) */
__attribute__((unused))
static int wifi_extract_ap_info(const uint8_t *frame,
                                uint16_t frame_len,
                                struct wifi_ap_info *ap_info,
                                int rssi,
                                uint8_t channel) {
    uint16_t offset;

    if (!frame || frame_len < 36u || !ap_info) {
        return -1;
    }

    offset = 36;  /* Size of beacon structure without IEs */

    /* Initialize AP info */
    memset(ap_info, 0, sizeof(struct wifi_ap_info));
    ap_info->rssi = rssi;
    ap_info->channel = channel;
    memcpy(ap_info->bssid.octets, ((struct wifi_mac_header *)frame)->addr3, 6);

    /* Parse information elements */
    while (offset + 2 <= frame_len) {
        uint8_t ie_type = frame[offset];
        uint8_t ie_len = frame[offset + 1];

        if (offset + 2 + ie_len > frame_len) {
            break;
        }

        switch (ie_type) {
            case IE_TYPE_SSID:
                if (ie_len > 0 && ie_len <= 32) {
                    memcpy(ap_info->ssid, &frame[offset + 2], ie_len);
                    ap_info->ssid[ie_len] = '\0';
                }
                break;

            case IE_TYPE_RSN:
                ap_info->security |= WIFI_SEC_WPA2;
                break;

            case IE_TYPE_DS_PARAM:
                if (ie_len >= 1) {
                    ap_info->channel = frame[offset + 2];
                }
                break;

            default:
                break;
        }

        offset += 2 + ie_len;
    }

    if (ap_info->ssid[0] == '\0') {
        strcpy((char *)ap_info->ssid, "<hidden>");
    }

    return 0;
}

/* Simulate scan by adding fake APs */
int wifi_device_scan_full(struct wifi_device *dev) {
    /* Fake APs for simulation */
    static const struct {
        const char *ssid;
        uint8_t channel;
        int rssi;
        uint8_t security;
    } fake_aps[] = {
        { "VibeOS-Net", 6, -45, WIFI_SEC_WPA2 },
        { "MyWiFi", 1, -62, WIFI_SEC_WPA2 },
        { "Guest", 11, -75, WIFI_SEC_OPEN },
        { "Lab-5GHz", 36, -55, WIFI_SEC_WPA2 },
    };

    if (!dev) {
        return -1;
    }

    dev->scan_count = sizeof(fake_aps) / sizeof(fake_aps[0]);

    for (uint32_t i = 0; i < dev->scan_count; ++i) {
        struct wifi_ap_info *ap = &dev->scan_results[i];
        strcpy((char *)ap->ssid, fake_aps[i].ssid);
        ap->channel = fake_aps[i].channel;
        ap->rssi = fake_aps[i].rssi;
        ap->security = fake_aps[i].security;
        ap->standards = WIFI_STANDARD_802_11G;
        ap->bssid.octets[0] = 0x00u;
        ap->bssid.octets[1] = 0x1Au;
        ap->bssid.octets[2] = 0x2Bu;
        ap->bssid.octets[3] = i;
        ap->bssid.octets[4] = i >> 8;
        ap->bssid.octets[5] = i;
    }

    dev->connection_state = WIFI_STATE_IDLE;
    return 0;
}

/* Get scan results */
uint32_t wifi_get_scan_results(struct wifi_device *dev,
                               struct wifi_ap_info *results,
                               uint32_t max_results) {
    uint32_t count;

    if (!dev || !results || max_results == 0) {
        return 0;
    }

    count = (dev->scan_count < max_results) ? dev->scan_count : max_results;
    memcpy(results, dev->scan_results, count * sizeof(struct wifi_ap_info));

    return count;
}
