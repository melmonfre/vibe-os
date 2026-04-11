#ifndef KERNEL_DRIVERS_NETWORK_WIFI_STUB_H
#define KERNEL_DRIVERS_NETWORK_WIFI_STUB_H

#include <stdint.h>

#define WIFI_STUB_SSID_MAX_LEN 32u
#define WIFI_STUB_MAX_SCAN_RESULTS 8u

struct wifi_stub_ap {
    char ssid[WIFI_STUB_SSID_MAX_LEN + 1u];
    uint8_t bssid[6];
    int8_t rssi;          /* signal strength in dBm */
    uint8_t channel;
    uint8_t encrypted;    /* 1 = secured, 0 = open */
};

void     kernel_wifi_stub_init(void);
int      kernel_wifi_stub_present(void);
void     kernel_wifi_stub_scan(void);
uint32_t kernel_wifi_stub_scan_count(void);
int      kernel_wifi_stub_scan_result(uint32_t index, struct wifi_stub_ap *out);

#endif
