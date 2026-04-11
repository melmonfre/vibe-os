#include <kernel/driver_manager.h>
#include <kernel/drivers/network/wifi_stub.h>
#include <kernel/kernel.h>
#include <kernel/kernel_string.h>

static int g_registered = 0;
static int g_present    = 0;

/* Fake AP entries returned by scan */
static const struct wifi_stub_ap g_fake_aps[] = {
    {
        "VibeOS-Net",
        { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u },
        -45,
        6u,
        1u
    },
    {
        "OpenAir",
        { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x02u },
        -72,
        11u,
        0u
    }
};

#define WIFI_STUB_FAKE_AP_COUNT \
    (sizeof(g_fake_aps) / sizeof(g_fake_aps[0]))

static uint32_t g_scan_count = 0u;

static void wifi_stub_probe(void) {
    /*
     * Stub: always report the device as present.
     * A real driver would scan the PCI bus for a supported 802.11 chipset.
     */
    g_present = 1;
    kernel_debug_puts("network: wi-fi stub device detected\n");
}

void kernel_wifi_stub_init(void) {
    if (!g_registered) {
        register_driver("wifi-stub", "network", kernel_wifi_stub_init);
        g_registered = 1;
        return;
    }

    wifi_stub_probe();
}

int kernel_wifi_stub_present(void) {
    return g_present;
}

void kernel_wifi_stub_scan(void) {
    if (!g_present) {
        g_scan_count = 0u;
        return;
    }
    g_scan_count = (uint32_t)WIFI_STUB_FAKE_AP_COUNT;
    kernel_debug_puts("network: wi-fi stub scan complete\n");
}

uint32_t kernel_wifi_stub_scan_count(void) {
    return g_scan_count;
}

int kernel_wifi_stub_scan_result(uint32_t index, struct wifi_stub_ap *out) {
    if (out == 0 || index >= g_scan_count) {
        return -1;
    }
    memcpy(out, &g_fake_aps[index], sizeof(*out));
    return 0;
}
