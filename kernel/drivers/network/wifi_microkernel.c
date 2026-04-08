#include <kernel/drivers/network/wifi_data_plane.h>
#include <kernel/microkernel/network.h>
#include <kernel/kernel_string.h>
#include <kernel/kernel.h>

#define WIFI_VENDOR_INTEL   0x8086u
#define WIFI_DEVICE_7260    0x08B1u

/* Externals from other WiFi modules */
extern int wifi_device_scan_full(struct wifi_device *dev);
extern uint32_t wifi_get_scan_results(struct wifi_device *dev,
                                       struct wifi_ap_info *results,
                                       uint32_t max_results);

/* Global WiFi device (populated during init) */
static struct wifi_device *g_wifi_device = 0;

/* Initialize WiFi data plane for microkernel use */
int wifi_microkernel_init(struct wifi_device *dev) {
    if (!dev) {
        return -1;
    }

    g_wifi_device = dev;
    
    /* Initialize device */
    if (wifi_device_init(dev, WIFI_VENDOR_INTEL, WIFI_DEVICE_7260) != 0) {
        return -1;
    }

    kernel_debug_puts("wifi: microkernel integration initialized\n");
    return 0;
}

/* Perform WiFi scan for microkernel */
int wifi_microkernel_scan(struct mk_network_wifi_scan_result *result) {
    struct wifi_ap_info scan_results[32];
    uint32_t count;
    uint32_t i;

    if (!result || !g_wifi_device) {
        return -1;
    }

    /* Perform scan */
    if (wifi_device_scan_full(g_wifi_device) != 0) {
        return -1;
    }

    /* Retrieve results */
    count = wifi_get_scan_results(g_wifi_device, scan_results, 32);
    if (count > MK_NETWORK_WIFI_SCAN_MAX_APS) {
        count = MK_NETWORK_WIFI_SCAN_MAX_APS;
    }

    /* Convert to microkernel format */
    result->count = count;
    for (i = 0u; i < count; ++i) {
        struct mk_network_wifi_ap *ap = &result->aps[i];
        const struct wifi_ap_info *wifi_ap = &scan_results[i];

        strncpy((char *)ap->ssid, (const char *)wifi_ap->ssid, 32);
        ap->ssid[32] = '\0';
        ap->rssi = (int8_t)wifi_ap->rssi;
        ap->channel = wifi_ap->channel;
        ap->encrypted = (wifi_ap->security != WIFI_SEC_OPEN) ? 1u : 0u;
    }

    return 0;
}

/* Connect to WiFi network */
int wifi_microkernel_connect(const char *ssid, const char *passphrase) {
    struct wifi_credentials creds;

    if (!ssid || !g_wifi_device) {
        return -1;
    }

    memset(&creds, 0, sizeof(creds));
    strncpy((char *)creds.ssid, ssid, 32);
    creds.ssid[32] = '\0';

    if (passphrase) {
        strncpy((char *)creds.passphrase, passphrase, 63);
        creds.passphrase[63] = '\0';
        creds.security = WIFI_SEC_WPA2;
    } else {
        creds.security = WIFI_SEC_OPEN;
    }

    if (wifi_device_connect(g_wifi_device, &creds) != 0) {
        return -1;
    }

    return 0;
}

/* Get current WiFi state */
int wifi_microkernel_get_state(void) {
    if (!g_wifi_device) {
        return WIFI_STATE_ERROR;
    }

    return wifi_device_get_state(g_wifi_device);
}

/* Get WiFi device info */
struct wifi_device *wifi_microkernel_get_device(void) {
    return g_wifi_device;
}
