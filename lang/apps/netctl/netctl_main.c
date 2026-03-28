#include <lang/include/vibe_app_runtime.h>
#include <lang/include/vibe_stdlib.h>
#include <sys/socket.h>

#define NETCTL_NETWORK_SETTINGS_PATH "/config/network.cfg"
#define NETCTL_NETMGRD_STATUS_PATH "/runtime/netmgrd-status.txt"
#define NETCTL_PROFILE_MAX 4

struct netctl_profile {
    int used;
    char ssid[MK_NETWORK_SSID_MAX + 1];
    char psk[MK_NETWORK_PSK_MAX + 1];
};

static void netctl_resolve_ethernet_if_name(char *buffer, int buffer_size) {
    struct mk_network_status status;

    if (buffer == 0 || buffer_size <= 0) {
        return;
    }
    snprintf(buffer, (size_t)buffer_size, "%s", "em0");
    if (vibe_app_network_get_status(&status) != 0) {
        return;
    }
    if (status.active_if[0] == '\0') {
        return;
    }
    if (strcmp(status.active_if, "wlan0") == 0 || strcmp(status.active_if, "lo0") == 0) {
        return;
    }
    snprintf(buffer, (size_t)buffer_size, "%s", status.active_if);
}

static void netctl_usage(void) {
    printf("usage: netctl <command> [args]\n");
    printf("commands:\n");
    printf("  status\n");
    printf("  scan [wlan0]\n");
    printf("  connect wlan0 <ssid> [--psk <senha>]\n");
    printf("  connect ethernet\n");
    printf("  disconnect [wlan0|ethernet]\n");
    printf("  profiles\n");
    printf("  remember wlan0 <ssid> [--psk <senha>]\n");
    printf("  forget <ssid>\n");
    printf("  autoconnect <ssid|off>\n");
    printf("  socket-smoke\n");
}

static const char *netctl_link_state_name(uint32_t state) {
    switch (state) {
    case MK_NETWORK_LINK_CONNECTED:
        return "connected";
    case MK_NETWORK_LINK_CONNECTING:
        return "connecting";
    default:
        return "disconnected";
    }
}

static const char *netctl_if_kind_name(uint32_t kind) {
    switch (kind) {
    case MK_NETWORK_IF_WIFI:
        return "wifi";
    case MK_NETWORK_IF_ETHERNET:
        return "ethernet";
    case MK_NETWORK_IF_LOOPBACK:
        return "loopback";
    default:
        return "unknown";
    }
}

static const char *netctl_lease_state_name(const struct mk_network_status *status) {
    if (status == 0) {
        return "indisponivel";
    }
    if (status->link_state == MK_NETWORK_LINK_CONNECTING) {
        return "adquirindo";
    }
    if (status->link_state == MK_NETWORK_LINK_CONNECTED) {
        if (status->ip_address[0] != '\0') {
            return "bound";
        }
        return "link-only";
    }
    return "nenhum";
}

static int netctl_wait_for_ethernet_ready(struct mk_network_status *status) {
    unsigned int started_at;
    unsigned int now;

    if (status == 0) {
        return -1;
    }
    if (vibe_app_network_get_status(status) != 0) {
        return -1;
    }
    started_at = vibe_app_ticks();
    now = started_at;
    while ((now - started_at) < 25u) {
        if (status->link_state != MK_NETWORK_LINK_CONNECTING ||
            status->active_kind != MK_NETWORK_IF_ETHERNET) {
            return 0;
        }
        (void)vibe_app_sleep_ms(10u);
        if (vibe_app_network_get_status(status) != 0) {
            return -1;
        }
        now = vibe_app_ticks();
    }
    return 0;
}

static int netctl_load_profiles(struct netctl_profile *profiles,
                                char *auto_ssid,
                                int auto_ssid_size) {
    const char *data = 0;
    int size = 0;
    char text[512];
    char *line;

    if (profiles == 0 || auto_ssid == 0 || auto_ssid_size <= 0) {
        return -1;
    }

    memset(profiles, 0, sizeof(struct netctl_profile) * NETCTL_PROFILE_MAX);
    auto_ssid[0] = '\0';

    if (vibe_app_read_file(NETCTL_NETWORK_SETTINGS_PATH, &data, &size) != 0 || data == 0 || size <= 0) {
        return 0;
    }

    if (size >= (int)sizeof(text)) {
        size = (int)sizeof(text) - 1;
    }
    memcpy(text, data, (size_t)size);
    text[size] = '\0';

    line = text;
    while (*line != '\0') {
        char *next = line;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }

        if (strncmp(line, "auto_ssid=", 10u) == 0) {
            snprintf(auto_ssid, (size_t)auto_ssid_size, "%s", line + 10);
        } else if (strncmp(line, "profile", 7u) == 0) {
            char *cursor = line + 7;
            int index = -1;

            while (*cursor >= '0' && *cursor <= '9') {
                if (index < 0) {
                    index = 0;
                }
                index = (index * 10) + (*cursor - '0');
                ++cursor;
            }
            if (index >= 0 && index < NETCTL_PROFILE_MAX) {
                if (strncmp(cursor, "_ssid=", 6u) == 0) {
                    profiles[index].used = 1;
                    snprintf(profiles[index].ssid, sizeof(profiles[index].ssid), "%s", cursor + 6);
                } else if (strncmp(cursor, "_psk=", 5u) == 0) {
                    profiles[index].used = 1;
                    snprintf(profiles[index].psk, sizeof(profiles[index].psk), "%s", cursor + 5);
                }
            }
        }

        line = next;
    }

    return 0;
}

static int netctl_save_profiles(const struct netctl_profile *profiles, const char *auto_ssid) {
    char text[512];
    int i;

    (void)vibe_app_create_dir("/config");

    text[0] = '\0';
    snprintf(text, sizeof(text), "auto_ssid=%s\n", auto_ssid != 0 ? auto_ssid : "");
    for (i = 0; i < NETCTL_PROFILE_MAX; ++i) {
        int len;

        if (profiles == 0 || !profiles[i].used) {
            continue;
        }
        len = (int)strlen(text);
        if (len >= (int)sizeof(text) - 1) {
            break;
        }
        snprintf(text + len,
                 sizeof(text) - (size_t)len,
                 "profile%d_ssid=%s\nprofile%d_psk=%s\n",
                 i, profiles[i].ssid, i, profiles[i].psk);
    }
    return vibe_app_write_file(NETCTL_NETWORK_SETTINGS_PATH, text, (int)strlen(text));
}

static struct netctl_profile *netctl_find_profile(struct netctl_profile *profiles, const char *ssid) {
    int i;

    if (profiles == 0 || ssid == 0 || *ssid == '\0') {
        return 0;
    }
    for (i = 0; i < NETCTL_PROFILE_MAX; ++i) {
        if (profiles[i].used && strcmp(profiles[i].ssid, ssid) == 0) {
            return &profiles[i];
        }
    }
    return 0;
}

static struct netctl_profile *netctl_find_or_allocate_profile(struct netctl_profile *profiles, const char *ssid) {
    struct netctl_profile *free_slot = 0;
    int i;

    if (profiles == 0 || ssid == 0 || *ssid == '\0') {
        return 0;
    }
    for (i = 0; i < NETCTL_PROFILE_MAX; ++i) {
        if (profiles[i].used) {
            if (strcmp(profiles[i].ssid, ssid) == 0) {
                return &profiles[i];
            }
        } else if (free_slot == 0) {
            free_slot = &profiles[i];
        }
    }
    return free_slot != 0 ? free_slot : &profiles[0];
}

static int netctl_read_status_value(const char *key, char *value, int value_size) {
    const char *data = 0;
    int size = 0;
    char text[512];
    char *line;
    int key_len;

    if (key == 0 || value == 0 || value_size <= 0) {
        return -1;
    }
    value[0] = '\0';
    if (vibe_app_read_file(NETCTL_NETMGRD_STATUS_PATH, &data, &size) != 0 || data == 0 || size <= 0) {
        return -1;
    }
    if (size >= (int)sizeof(text)) {
        size = (int)sizeof(text) - 1;
    }
    memcpy(text, data, (size_t)size);
    text[size] = '\0';
    key_len = (int)strlen(key);
    line = text;
    while (*line != '\0') {
        char *next = line;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }
        if (strncmp(line, key, (size_t)key_len) == 0 && line[key_len] == '=') {
            snprintf(value, (size_t)value_size, "%s", line + key_len + 1);
            return 0;
        }
        line = next;
    }
    return -1;
}

static int netctl_command_status(void) {
    struct mk_network_info info;
    struct mk_network_status status;
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    char lease_source[24];
    int saved_count = 0;

    if (vibe_app_network_get_info(&info) != 0 || vibe_app_network_get_status(&status) != 0) {
        printf("netctl: network service unavailable\n");
        return 1;
    }

    (void)netctl_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    for (int i = 0; i < NETCTL_PROFILE_MAX; ++i) {
        if (profiles[i].used) {
            saved_count += 1;
        }
    }

    printf("caps: 0x%x\n", (unsigned int)info.flags);
    printf("families: 0x%x  sockets: 0x%x\n",
           (unsigned int)info.supported_families,
           (unsigned int)info.supported_socket_types);
    printf("state: %s\n", netctl_link_state_name(status.link_state));
    printf("active: %s (%s)\n",
           status.active_if[0] != '\0' ? status.active_if : "-",
           netctl_if_kind_name(status.active_kind));
    printf("ip: %s\n", status.ip_address[0] != '\0' ? status.ip_address : "-");
    printf("gateway: %s\n", status.gateway[0] != '\0' ? status.gateway : "-");
    printf("dns: %s\n", status.dns_server[0] != '\0' ? status.dns_server : "-");
    printf("lease: %s\n", netctl_lease_state_name(&status));
    if (netctl_read_status_value("lease_source", lease_source, (int)sizeof(lease_source)) == 0) {
        printf("lease source: %s\n", lease_source[0] != '\0' ? lease_source : "-");
    }
    printf("ssid: %s\n", status.current_ssid[0] != '\0' ? status.current_ssid : "-");
    printf("visible wifi: %u\n", (unsigned int)status.visible_network_count);
    printf("saved profiles: %d\n", saved_count);
    printf("autoconnect: %s\n", auto_ssid[0] != '\0' ? auto_ssid : "-");
    return 0;
}

static int netctl_command_scan(const char *if_name) {
    struct mk_network_scan_info info;
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int count = 0;

    if (if_name != 0 && strcmp(if_name, "wlan0") != 0) {
        printf("netctl: only wlan0 is supported in this MVP\n");
        return 1;
    }

    (void)netctl_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));

    while (vibe_app_network_scan((uint32_t)count, &info) == 0) {
        struct netctl_profile *profile = netctl_find_profile(profiles, info.ssid);

        printf("%s  signal=%u  %s%s%s%s\n",
               info.ssid,
               (unsigned int)info.signal_strength,
               info.security == MK_NETWORK_SECURITY_OPEN ? "open" : "wpa-psk",
               info.connected ? "  [connected]" : "",
               profile != 0 ? "  [saved]" : "",
               (auto_ssid[0] != '\0' && strcmp(auto_ssid, info.ssid) == 0) ? "  [auto]" : "");
        count += 1;
    }

    if (count == 0) {
        printf("no wifi networks found\n");
    }
    return 0;
}

static int netctl_command_connect(int argc, char **argv) {
    struct mk_network_status status;
    struct mk_network_connect_request request;
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];
    struct netctl_profile *profile;
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int i;

    if (argc < 3) {
        netctl_usage();
        return 1;
    }
    if (strcmp(argv[2], "em0") == 0 || strcmp(argv[2], "ethernet") == 0) {
        char if_name[MK_NETWORK_IF_NAME_MAX];

        netctl_resolve_ethernet_if_name(if_name, (int)sizeof(if_name));
        if (argc != 3) {
            printf("usage: netctl connect ethernet\n");
            return 1;
        }
        if (vibe_app_network_connect_ethernet(if_name) != 0) {
            printf("netctl: failed to connect %s\n", if_name);
            return 1;
        }
        if (netctl_wait_for_ethernet_ready(&status) == 0) {
            printf("connected %s (%s) ip=%s dns=%s\n",
                   status.active_if[0] != '\0' ? status.active_if : if_name,
                   netctl_if_kind_name(status.active_kind),
                   status.ip_address[0] != '\0' ? status.ip_address : "-",
                   status.dns_server[0] != '\0' ? status.dns_server : "-");
        } else {
            printf("connected %s\n", if_name);
        }
        return 0;
    }
    if (strcmp(argv[2], "wlan0") != 0) {
        printf("netctl: supported interfaces in this MVP are wlan0 and ethernet\n");
        return 1;
    }
    if (argc < 4) {
        netctl_usage();
        return 1;
    }

    memset(&request, 0, sizeof(request));
    strcpy(request.if_name, argv[2]);
    strcpy(request.ssid, argv[3]);
    (void)netctl_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    for (i = 4; i < argc; ++i) {
        if (strcmp(argv[i], "--psk") == 0 && i + 1 < argc) {
            strcpy(request.psk, argv[i + 1]);
            i += 1;
        }
    }

    if (request.psk[0] == '\0') {
        profile = netctl_find_profile(profiles, request.ssid);
        if (profile != 0) {
            strcpy(request.psk, profile->psk);
        }
    }

    if (vibe_app_network_connect_wifi(&request) != 0) {
        printf("netctl: failed to connect to %s\n", request.ssid);
        return 1;
    }

    profile = netctl_find_or_allocate_profile(profiles, request.ssid);
    if (profile != 0) {
        profile->used = 1;
        strcpy(profile->ssid, request.ssid);
        strcpy(profile->psk, request.psk);
        strcpy(auto_ssid, request.ssid);
        (void)netctl_save_profiles(profiles, auto_ssid);
    }

    printf("connected to %s via %s\n", request.ssid, request.if_name);
    return 0;
}

static int netctl_command_disconnect(const char *if_name) {
    const char *target = if_name != 0 ? if_name : "wlan0";

    if (vibe_app_network_disconnect(target) != 0) {
        printf("netctl: failed to disconnect %s\n", target);
        return 1;
    }

    printf("disconnected %s\n", target);
    return 0;
}

static int netctl_command_profiles(void) {
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int count = 0;

    (void)netctl_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    for (int i = 0; i < NETCTL_PROFILE_MAX; ++i) {
        if (!profiles[i].used) {
            continue;
        }
        printf("%s%s\n",
               profiles[i].ssid,
               (auto_ssid[0] != '\0' && strcmp(auto_ssid, profiles[i].ssid) == 0) ? "  [auto]" : "");
        count += 1;
    }
    if (count == 0) {
        printf("no saved profiles\n");
    }
    return 0;
}

static int netctl_command_remember(int argc, char **argv) {
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];
    struct netctl_profile *profile;
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    const char *psk = "";

    if (argc < 4) {
        netctl_usage();
        return 1;
    }
    if (strcmp(argv[2], "wlan0") != 0) {
        printf("netctl: only wlan0 is supported in this MVP\n");
        return 1;
    }

    (void)netctl_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    for (int i = 4; i < argc; ++i) {
        if (strcmp(argv[i], "--psk") == 0 && i + 1 < argc) {
            psk = argv[i + 1];
            i += 1;
        }
    }

    profile = netctl_find_or_allocate_profile(profiles, argv[3]);
    if (profile == 0) {
        return 1;
    }
    profile->used = 1;
    strcpy(profile->ssid, argv[3]);
    strcpy(profile->psk, psk);
    if (auto_ssid[0] == '\0') {
        strcpy(auto_ssid, argv[3]);
    }
    if (netctl_save_profiles(profiles, auto_ssid) != 0) {
        printf("netctl: failed to save profile\n");
        return 1;
    }
    printf("saved profile %s\n", argv[3]);
    return 0;
}

static int netctl_command_forget(const char *ssid) {
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int found = 0;

    if (ssid == 0 || *ssid == '\0') {
        netctl_usage();
        return 1;
    }

    (void)netctl_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    for (int i = 0; i < NETCTL_PROFILE_MAX; ++i) {
        if (profiles[i].used && strcmp(profiles[i].ssid, ssid) == 0) {
            memset(&profiles[i], 0, sizeof(profiles[i]));
            found = 1;
        }
    }
    if (!found) {
        printf("netctl: profile not found for %s\n", ssid);
        return 1;
    }
    if (strcmp(auto_ssid, ssid) == 0) {
        auto_ssid[0] = '\0';
    }
    if (netctl_save_profiles(profiles, auto_ssid) != 0) {
        printf("netctl: failed to save profiles\n");
        return 1;
    }
    printf("forgot profile %s\n", ssid);
    return 0;
}

static int netctl_command_autoconnect(const char *ssid) {
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];

    if (ssid == 0 || *ssid == '\0') {
        netctl_usage();
        return 1;
    }

    (void)netctl_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    if (strcmp(ssid, "off") == 0) {
        auto_ssid[0] = '\0';
    } else if (netctl_find_profile(profiles, ssid) == 0) {
        printf("netctl: saved profile not found for %s\n", ssid);
        return 1;
    } else {
        strcpy(auto_ssid, ssid);
    }

    if (netctl_save_profiles(profiles, auto_ssid) != 0) {
        printf("netctl: failed to update autoconnect\n");
        return 1;
    }
    printf("autoconnect %s\n", auto_ssid[0] != '\0' ? auto_ssid : "disabled");
    return 0;
}

static int netctl_command_socket_smoke(void) {
    struct sockaddr bind_addr;
    struct sockaddr connect_addr;
    char recv_buf[32];
    const char *payload = "ping";
    int server = -1;
    int accepted = -1;
    int client = -1;
    int rc = 1;
    int received;

    memset(&bind_addr, 0, sizeof(bind_addr));
    memset(&connect_addr, 0, sizeof(connect_addr));
    bind_addr.sa_len = sizeof(bind_addr);
    bind_addr.sa_family = AF_UNIX;
    strcpy(bind_addr.sa_data, "net-smoke");
    connect_addr = bind_addr;

    server = vibe_app_network_socket(AF_UNIX, SOCK_STREAM, 0);
    client = vibe_app_network_socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0 || client < 0) {
        printf("netctl: failed to allocate socket handles\n");
        goto cleanup;
    }
    if (vibe_app_network_bind(server, &bind_addr, sizeof(bind_addr)) != 0) {
        printf("netctl: bind failed\n");
        goto cleanup;
    }
    if (vibe_app_network_listen(server, 1) != 0) {
        printf("netctl: listen failed\n");
        goto cleanup;
    }
    if (vibe_app_network_socket_connect(client, &connect_addr, sizeof(connect_addr)) != 0) {
        printf("netctl: connect failed\n");
        goto cleanup;
    }
    accepted = vibe_app_network_accept(server);
    if (accepted < 0) {
        printf("netctl: accept failed\n");
        goto cleanup;
    }
    if (vibe_app_network_send(client, payload, 4u) != 4) {
        printf("netctl: send failed\n");
        goto cleanup;
    }

    memset(recv_buf, 0, sizeof(recv_buf));
    received = vibe_app_network_recv(accepted, recv_buf, sizeof(recv_buf) - 1u);
    if (received != 4 || strcmp(recv_buf, payload) != 0) {
        printf("netctl: recv failed (%d)\n", received);
        goto cleanup;
    }

    printf("socket smoke ok: %s\n", recv_buf);
    rc = 0;

cleanup:
    if (client >= 0) {
        (void)vibe_app_network_close(client);
    }
    if (accepted >= 0) {
        (void)vibe_app_network_close(accepted);
    }
    if (server >= 0) {
        (void)vibe_app_network_close(server);
    }
    return rc;
}

int vibe_app_main(int argc, char **argv) {
    if (argc < 2) {
        netctl_usage();
        return 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        return netctl_command_status();
    }
    if (strcmp(argv[1], "scan") == 0) {
        return netctl_command_scan(argc >= 3 ? argv[2] : "wlan0");
    }
    if (strcmp(argv[1], "connect") == 0) {
        return netctl_command_connect(argc, argv);
    }
    if (strcmp(argv[1], "disconnect") == 0) {
        return netctl_command_disconnect(argc >= 3 ? argv[2] : "wlan0");
    }
    if (strcmp(argv[1], "profiles") == 0) {
        return netctl_command_profiles();
    }
    if (strcmp(argv[1], "remember") == 0) {
        return netctl_command_remember(argc, argv);
    }
    if (strcmp(argv[1], "forget") == 0) {
        return netctl_command_forget(argc >= 3 ? argv[2] : 0);
    }
    if (strcmp(argv[1], "autoconnect") == 0) {
        return netctl_command_autoconnect(argc >= 3 ? argv[2] : 0);
    }
    if (strcmp(argv[1], "socket-smoke") == 0) {
        return netctl_command_socket_smoke();
    }

    netctl_usage();
    return 1;
}
