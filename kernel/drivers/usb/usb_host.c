#include <kernel/drivers/usb/usb_host.h>

#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/hal/io.h>
#include <kernel/kernel_string.h>

#define PCI_CLASS_SERIAL_BUS 0x0Cu
#define PCI_SUBCLASS_USB 0x03u

#define PCI_PROGIF_USB_UHCI 0x00u
#define PCI_PROGIF_USB_OHCI 0x10u
#define PCI_PROGIF_USB_EHCI 0x20u
#define PCI_PROGIF_USB_XHCI 0x30u

#define KERNEL_USB_HOST_MAX_CONTROLLERS 16u
#define KERNEL_USB_HOST_MAX_ROOT_DEVICES 64u
#define KERNEL_USB_MAX_DEVICES 64u
#define KERNEL_USB_MAX_PROBE_SNAPSHOTS 64u

#define EHCI_CAPLENGTH 0x00u
#define EHCI_HCSPARAMS 0x04u
#define EHCI_PORTSC_BASE 0x44u
#define OHCI_RH_DESCRIPTOR_A 0x48u
#define OHCI_RH_PORT_STATUS_BASE 0x54u
#define OHCI_REVISION 0x00u
#define OHCI_CONTROL 0x04u
#define OHCI_COMMAND_STATUS 0x08u
#define OHCI_INTERRUPT_STATUS 0x0cu
#define OHCI_INTERRUPT_DISABLE 0x14u
#define OHCI_HCCA 0x18u
#define OHCI_CONTROL_HEAD_ED 0x20u
#define OHCI_CONTROL_CURRENT_ED 0x24u
#define OHCI_DONE_HEAD 0x30u
#define OHCI_ALL_INTRS 0x4000007fu
#define OHCI_MIE 0x80000000u
#define OHCI_CLE 0x00000010u
#define OHCI_HCFS_MASK 0x000000c0u
#define OHCI_HCFS_OPERATIONAL 0x00000080u
#define OHCI_CLF 0x00000002u
#define OHCI_ED_SPEED 0x00002000u
#define OHCI_ED_SET_FA(s) ((uint32_t)(s))
#define OHCI_ED_SET_EN(s) (((uint32_t)(s) & 0x0fu) << 7)
#define OHCI_ED_SET_MAXP(s) (((uint32_t)(s) & 0x7ffu) << 16)
#define OHCI_TD_SETUP 0x00000000u
#define OHCI_TD_OUT 0x00080000u
#define OHCI_TD_IN 0x00100000u
#define OHCI_TD_SET_DI(x) (((uint32_t)(x) & 0x7u) << 21)
#define OHCI_TD_TOGGLE_0 0x02000000u
#define OHCI_TD_TOGGLE_1 0x03000000u
#define OHCI_TD_NOCC 0xf0000000u
#define OHCI_TD_GET_CC(x) (((uint32_t)(x)) >> 28)
#define XHCI_HCSPARAMS1 0x04u
#define XHCI_CAPLENGTH 0x00u
#define XHCI_PORTSC_BASE 0x400u

#define UHCI_PORTSC1 0x10u
#define UHCI_CMD 0x00u
#define UHCI_STS 0x02u
#define UHCI_INTR 0x04u
#define UHCI_FRNUM 0x06u
#define UHCI_FLBASEADDR 0x08u
#define UHCI_CMD_RS 0x0001u
#define UHCI_CMD_CF 0x0040u
#define UHCI_STS_ALLINTRS 0x003fu
#define UHCI_TD_ACTIVE 0x00800000u
#define UHCI_TD_IOC 0x01000000u
#define UHCI_TD_LS 0x04000000u
#define UHCI_PTR_T 0x00000001u
#define UHCI_PTR_TD 0x00000000u
#define UHCI_PTR_QH 0x00000002u
#define UHCI_PTR_VF 0x00000004u
#define UHCI_FRAMELIST_COUNT 1024u

#define USB_PORT_CONNECTED 0x00000001u
#define USB_PORT_ENABLED 0x00000002u
#define USB_PORT_RESET_ACTIVE 0x00000010u
#define USB_PORT_POWERED 0x00001000u
#define USB_PORT_OWNER 0x00002000u

#define UHCI_PORT_LOW_SPEED 0x00000100u

#define OHCI_PORT_RESET_STATUS 0x00000010u
#define OHCI_PORT_POWER_STATUS 0x00000100u
#define OHCI_PORT_LOW_SPEED 0x00000200u

#define EHCI_PORT_RESET 0x00000100u
#define EHCI_PORT_LINE_STATUS_SHIFT 10u
#define EHCI_PORT_LINE_STATUS_MASK 0x3u

#define XHCI_PORT_RESET 0x00000010u
#define XHCI_PORT_POWER 0x00000200u
#define XHCI_PORT_SPEED_SHIFT 10u
#define XHCI_PORT_SPEED_MASK 0x0fu

#define USB_REQTYPE_IN 0x80u
#define USB_REQTYPE_STANDARD 0x00u
#define USB_REQTYPE_DEVICE 0x00u
#define USB_REQUEST_GET_DESCRIPTOR 0x06u
#define USB_DESCRIPTOR_TYPE_DEVICE 0x01u
#define USB_DESCRIPTOR_TYPE_CONFIG 0x02u
#define USB_DEVICE_DESCRIPTOR_PROBE_LENGTH 8u
#define USB_DEVICE_DESCRIPTOR_FULL_LENGTH 18u
#define USB_CONFIG_DESCRIPTOR_SHORT_LENGTH 9u
#define USB_CONFIG_DESCRIPTOR_MAX_LENGTH 512u
#define USB_DESCRIPTOR_TYPE_INTERFACE 0x04u
#define USB_DESCRIPTOR_TYPE_ENDPOINT 0x05u
#define USB_CLASS_AUDIO 0x01u
#define USB_SUBCLASS_AUDIOCONTROL 0x01u
#define USB_SUBCLASS_AUDIOSTREAM 0x02u
#define USB_SETUP_PACKET_LENGTH 8u
#define KERNEL_USB_UHCI_POLL_LIMIT 100000u

#define KERNEL_USB_UHCI_TD_SETUP(len, endp, dev) \
    ((((uint32_t)(len) - 1u) << 21) | (((uint32_t)(endp) & 0x0fu) << 15) | \
     (((uint32_t)(dev) & 0x7fu) << 8) | 0x2du)
#define KERNEL_USB_UHCI_TD_IN(len, endp, dev, dt) \
    ((((uint32_t)(len) - 1u) << 21) | (((uint32_t)(dt) & 0x1u) << 19) | \
     (((uint32_t)(endp) & 0x0fu) << 15) | (((uint32_t)(dev) & 0x7fu) << 8) | 0x69u)
#define KERNEL_USB_UHCI_TD_OUT(len, endp, dev, dt) \
    ((((uint32_t)(len) - 1u) << 21) | (((uint32_t)(dt) & 0x1u) << 19) | \
     (((uint32_t)(endp) & 0x0fu) << 15) | (((uint32_t)(dev) & 0x7fu) << 8) | 0xe1u)
#define KERNEL_USB_UHCI_TD_ERRCNT(n) (((uint32_t)(n) & 0x3u) << 27)
#define KERNEL_USB_UHCI_TD_GET_ACTLEN(s) ((((uint32_t)(s)) + 1u) & 0x3ffu)

struct kernel_usb_uhci_probe_qh {
    uint32_t qh_hlink;
    uint32_t qh_elink;
} __attribute__((aligned(16)));

struct kernel_usb_uhci_probe_td {
    uint32_t td_link;
    uint32_t td_status;
    uint32_t td_token;
    uint32_t td_buffer;
} __attribute__((aligned(16)));

struct kernel_usb_setup_packet {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} __attribute__((packed));

struct kernel_usb_ohci_probe_hcca {
    uint32_t interrupt_table[32];
    uint16_t frame_number;
    uint16_t pad;
    uint32_t done_head;
    uint8_t reserved[116];
} __attribute__((aligned(256)));

struct kernel_usb_ohci_probe_ed {
    uint32_t ed_flags;
    uint32_t ed_tailp;
    uint32_t ed_headp;
    uint32_t ed_nexted;
} __attribute__((aligned(16)));

struct kernel_usb_ohci_probe_td {
    uint32_t td_flags;
    uint32_t td_cbp;
    uint32_t td_nexttd;
    uint32_t td_be;
} __attribute__((aligned(16)));

struct kernel_usb_host_state {
    uint32_t controllers;
    uint32_t uhci;
    uint32_t ohci;
    uint32_t ehci;
    uint32_t xhci;
    uint32_t connected_ports;
    uint32_t connected_high_speed_ports;
    uint32_t connected_super_speed_ports;
    uint32_t root_devices;
    uint32_t audio_candidates;
    uint32_t devices;
    uint32_t devices_ready_for_enum;
    uint32_t devices_control_ready;
    uint32_t devices_needing_companion;
    uint32_t devices_with_companion_present;
    uint32_t devices_handoff_ready;
    uint32_t devices_control_path_ready;
    uint32_t device_probe_targets;
    uint32_t audio_probe_targets;
    uint32_t probe_snapshots;
    uint32_t audio_probe_snapshots;
    uint32_t probe_dispatch_ready;
    uint32_t audio_probe_dispatch_ready;
    uint32_t probe_exec_ready;
    uint32_t audio_probe_exec_ready;
    uint32_t probe_descriptor_ready;
    uint32_t audio_probe_descriptor_ready;
    uint32_t probe_dispatch_cursor;
    uint32_t audio_probe_dispatch_cursor;
    struct kernel_usb_host_controller_info entries[KERNEL_USB_HOST_MAX_CONTROLLERS];
    struct kernel_usb_host_root_device_info root_device_entries[KERNEL_USB_HOST_MAX_ROOT_DEVICES];
    struct kernel_usb_device_info device_entries[KERNEL_USB_MAX_DEVICES];
    struct kernel_usb_probe_snapshot probe_snapshot_entries[KERNEL_USB_MAX_PROBE_SNAPSHOTS];
};

static struct kernel_usb_host_state g_kernel_usb_host_state;
static uint32_t g_kernel_usb_uhci_probe_frame_list[UHCI_FRAMELIST_COUNT] __attribute__((aligned(4096)));
static struct kernel_usb_uhci_probe_qh g_kernel_usb_uhci_probe_qh;
static struct kernel_usb_uhci_probe_td g_kernel_usb_uhci_probe_setup_td;
static struct kernel_usb_uhci_probe_td g_kernel_usb_uhci_probe_data_td;
static struct kernel_usb_uhci_probe_td g_kernel_usb_uhci_probe_status_td;
static struct kernel_usb_setup_packet g_kernel_usb_uhci_probe_request __attribute__((aligned(16)));
static uint8_t g_kernel_usb_uhci_probe_data[USB_DEVICE_DESCRIPTOR_FULL_LENGTH] __attribute__((aligned(16)));
static struct kernel_usb_ohci_probe_hcca g_kernel_usb_ohci_probe_hcca;
static struct kernel_usb_ohci_probe_ed g_kernel_usb_ohci_probe_ed;
static struct kernel_usb_ohci_probe_td g_kernel_usb_ohci_probe_setup_td;
static struct kernel_usb_ohci_probe_td g_kernel_usb_ohci_probe_data_td;
static struct kernel_usb_ohci_probe_td g_kernel_usb_ohci_probe_status_td;
static struct kernel_usb_ohci_probe_td g_kernel_usb_ohci_probe_tail_td;
static struct kernel_usb_setup_packet g_kernel_usb_ohci_probe_request __attribute__((aligned(16)));
static uint8_t g_kernel_usb_ohci_probe_data[USB_DEVICE_DESCRIPTOR_FULL_LENGTH] __attribute__((aligned(16)));
static uint8_t g_kernel_usb_config_probe_data[USB_CONFIG_DESCRIPTOR_SHORT_LENGTH] __attribute__((aligned(16)));
static uint8_t g_kernel_usb_config_full_data[USB_CONFIG_DESCRIPTOR_MAX_LENGTH] __attribute__((aligned(16)));

static void kernel_usb_host_probe_snapshot_set_status(struct kernel_usb_host_state *state,
                                                      struct kernel_usb_probe_snapshot *snapshot,
                                                      uint8_t new_status);
static int kernel_usb_host_probe_execute_uhci(const struct kernel_usb_probe_dispatch_context *dispatch);
static int kernel_usb_host_probe_execute_ohci(const struct kernel_usb_probe_dispatch_context *dispatch);

static uint8_t kernel_usb_host_kind_from_prog_if(uint8_t prog_if) {
    switch (prog_if) {
    case PCI_PROGIF_USB_UHCI:
        return KERNEL_USB_HOST_KIND_UHCI;
    case PCI_PROGIF_USB_OHCI:
        return KERNEL_USB_HOST_KIND_OHCI;
    case PCI_PROGIF_USB_EHCI:
        return KERNEL_USB_HOST_KIND_EHCI;
    case PCI_PROGIF_USB_XHCI:
        return KERNEL_USB_HOST_KIND_XHCI;
    default:
        return KERNEL_USB_HOST_KIND_UNKNOWN;
    }
}

static const char *kernel_usb_host_kind_name(uint8_t kind) {
    switch (kind) {
    case KERNEL_USB_HOST_KIND_UHCI:
        return "uhci";
    case KERNEL_USB_HOST_KIND_OHCI:
        return "ohci";
    case KERNEL_USB_HOST_KIND_EHCI:
        return "ehci";
    case KERNEL_USB_HOST_KIND_XHCI:
        return "xhci";
    default:
        return "usb";
    }
}

static uint8_t kernel_usb_host_transport_kind(const struct kernel_usb_host_controller_info *controller) {
    if (controller == 0) {
        return KERNEL_USB_HOST_KIND_UNKNOWN;
    }
    switch (controller->kind) {
    case KERNEL_USB_HOST_KIND_UHCI:
    case KERNEL_USB_HOST_KIND_OHCI:
    case KERNEL_USB_HOST_KIND_EHCI:
    case KERNEL_USB_HOST_KIND_XHCI:
        return controller->kind;
    default:
        return KERNEL_USB_HOST_KIND_UNKNOWN;
    }
}

static uint8_t kernel_usb_host_transport_available(uint8_t transport_kind) {
    switch (transport_kind) {
    case KERNEL_USB_HOST_KIND_UHCI:
    case KERNEL_USB_HOST_KIND_OHCI:
        return 1u;
    default:
        return 0u;
    }
}

static uint8_t kernel_usb_probe_result_for_transport(uint8_t transport_kind) {
    switch (transport_kind) {
    case KERNEL_USB_HOST_KIND_UHCI:
        return KERNEL_USB_PROBE_EXEC_RESULT_UHCI_UNAVAILABLE;
    case KERNEL_USB_HOST_KIND_OHCI:
        return KERNEL_USB_PROBE_EXEC_RESULT_OHCI_UNAVAILABLE;
    case KERNEL_USB_HOST_KIND_EHCI:
        return KERNEL_USB_PROBE_EXEC_RESULT_EHCI_UNAVAILABLE;
    case KERNEL_USB_HOST_KIND_XHCI:
        return KERNEL_USB_PROBE_EXEC_RESULT_XHCI_UNAVAILABLE;
    default:
        return KERNEL_USB_PROBE_EXEC_RESULT_NO_TRANSPORT;
    }
}

static const char *kernel_usb_host_speed_name(uint8_t speed) {
    switch (speed) {
    case KERNEL_USB_HOST_PORT_SPEED_LOW:
        return "low";
    case KERNEL_USB_HOST_PORT_SPEED_FULL:
        return "full";
    case KERNEL_USB_HOST_PORT_SPEED_HIGH:
        return "high";
    case KERNEL_USB_HOST_PORT_SPEED_SUPER:
        return "super";
    default:
        return "unknown";
    }
}

static uint8_t kernel_usb_host_port_looks_like_audio_candidate(const struct kernel_usb_host_controller_info *entry,
                                                               const struct kernel_usb_host_port_status *status) {
    if (entry == 0 || status == 0 || !status->connected) {
        return 0u;
    }

    if (status->speed == KERNEL_USB_HOST_PORT_SPEED_HIGH ||
        status->speed == KERNEL_USB_HOST_PORT_SPEED_SUPER) {
        return 1u;
    }

    if (status->speed == KERNEL_USB_HOST_PORT_SPEED_FULL &&
        status->powered &&
        (entry->kind == KERNEL_USB_HOST_KIND_UHCI ||
         entry->kind == KERNEL_USB_HOST_KIND_OHCI ||
         entry->kind == KERNEL_USB_HOST_KIND_XHCI)) {
        return 1u;
    }

    return 0u;
}

static uint8_t kernel_usb_host_port_is_ready_for_enum(const struct kernel_usb_host_port_status *status) {
    if (status == 0 || !status->connected) {
        return 0u;
    }
    return (uint8_t)((status->powered && !status->reset) ? 1u : 0u);
}

static uint8_t kernel_usb_host_port_needs_companion(const struct kernel_usb_host_controller_info *entry,
                                                    const struct kernel_usb_host_port_status *status) {
    if (entry == 0 || status == 0 || !status->connected) {
        return 0u;
    }
    return (uint8_t)(entry->kind == KERNEL_USB_HOST_KIND_EHCI && status->owner ? 1u : 0u);
}

static uint8_t kernel_usb_host_port_is_control_ready(const struct kernel_usb_host_controller_info *entry,
                                                     const struct kernel_usb_host_port_status *status) {
    if (entry == 0 || status == 0 || !status->connected || !status->powered || status->reset) {
        return 0u;
    }

    switch (entry->kind) {
    case KERNEL_USB_HOST_KIND_UHCI:
    case KERNEL_USB_HOST_KIND_OHCI:
        return 1u;
    case KERNEL_USB_HOST_KIND_EHCI:
        return (uint8_t)((!status->owner &&
                          status->speed == KERNEL_USB_HOST_PORT_SPEED_HIGH) ? 1u : 0u);
    case KERNEL_USB_HOST_KIND_XHCI:
        return 1u;
    default:
        return 0u;
    }
}

static uint8_t kernel_usb_host_find_companion_controller(const struct kernel_usb_host_state *state,
                                                         uint8_t controller_index,
                                                         uint8_t *companion_kind_out) {
    const struct kernel_usb_host_controller_info *entry;

    if (state == 0 || controller_index >= state->controllers) {
        return 0xffu;
    }

    entry = &state->entries[controller_index];
    if (entry->kind != KERNEL_USB_HOST_KIND_EHCI) {
        return 0xffu;
    }

    for (uint8_t i = 0u; i < state->controllers; ++i) {
        const struct kernel_usb_host_controller_info *candidate = &state->entries[i];

        if (i == controller_index) {
            continue;
        }
        if (candidate->bus != entry->bus || candidate->slot != entry->slot) {
            continue;
        }
        if (candidate->kind != KERNEL_USB_HOST_KIND_UHCI &&
            candidate->kind != KERNEL_USB_HOST_KIND_OHCI) {
            continue;
        }
        if (companion_kind_out != 0) {
            *companion_kind_out = candidate->kind;
        }
        return i;
    }

    return 0xffu;
}

static void kernel_usb_host_register_root_device(struct kernel_usb_host_state *state,
                                                 const struct kernel_usb_host_controller_info *entry,
                                                 uint8_t controller_index,
                                                 uint32_t port,
                                                 const struct kernel_usb_host_port_status *port_status) {
    struct kernel_usb_host_root_device_info *root_device;
    struct kernel_usb_device_info *device;
    uint8_t audio_candidate;
    uint8_t ready_for_enum;
    uint8_t control_ready;
    uint8_t needs_companion;
    uint8_t companion_controller_index;
    uint8_t companion_controller_kind;
    uint8_t effective_controller_index;
    uint8_t effective_controller_kind;
    uint8_t handoff_ready;
    uint8_t control_path_ready;

    if (state == 0 || entry == 0 || port_status == 0 || !port_status->connected) {
        return;
    }

    audio_candidate = kernel_usb_host_port_looks_like_audio_candidate(entry, port_status);
    ready_for_enum = kernel_usb_host_port_is_ready_for_enum(port_status);
    control_ready = kernel_usb_host_port_is_control_ready(entry, port_status);
    needs_companion = kernel_usb_host_port_needs_companion(entry, port_status);
    companion_controller_kind = KERNEL_USB_HOST_KIND_UNKNOWN;
    companion_controller_index = kernel_usb_host_find_companion_controller(state,
                                                                           controller_index,
                                                                           &companion_controller_kind);
    effective_controller_index = controller_index;
    effective_controller_kind = entry->kind;
    handoff_ready = 0u;
    control_path_ready = control_ready;
    if (needs_companion && companion_controller_index != 0xffu) {
        effective_controller_index = companion_controller_index;
        effective_controller_kind = companion_controller_kind;
        handoff_ready = 1u;
        control_path_ready = 1u;
    }

    if (state->root_devices < KERNEL_USB_HOST_MAX_ROOT_DEVICES) {
        root_device = &state->root_device_entries[state->root_devices];
        memset(root_device, 0, sizeof(*root_device));
        root_device->controller_index = controller_index;
        root_device->port_index = (uint8_t)port;
        root_device->controller_kind = entry->kind;
        root_device->speed = port_status->speed;
        if (port_status->connected) {
            root_device->flags |= KERNEL_USB_HOST_ROOT_DEVICE_FLAG_CONNECTED;
        }
        if (port_status->enabled) {
            root_device->flags |= KERNEL_USB_HOST_ROOT_DEVICE_FLAG_ENABLED;
        }
        if (port_status->powered) {
            root_device->flags |= KERNEL_USB_HOST_ROOT_DEVICE_FLAG_POWERED;
        }
        if (port_status->owner) {
            root_device->flags |= KERNEL_USB_HOST_ROOT_DEVICE_FLAG_OWNER;
        }
        state->root_devices++;
    }

    if (state->devices < KERNEL_USB_MAX_DEVICES) {
        device = &state->device_entries[state->devices];
        memset(device, 0, sizeof(*device));
        device->address = (uint8_t)(state->devices + 1u);
        device->controller_index = controller_index;
        device->companion_controller_index = companion_controller_index;
        device->effective_controller_index = effective_controller_index;
        device->port_index = (uint8_t)port;
        device->controller_kind = entry->kind;
        device->companion_controller_kind = companion_controller_kind;
        device->effective_controller_kind = effective_controller_kind;
        device->speed = port_status->speed;
        device->state = control_ready ? KERNEL_USB_DEVICE_STATE_READY_FOR_CONTROL :
                        (handoff_ready ? KERNEL_USB_DEVICE_STATE_READY_FOR_HANDOFF :
                         (needs_companion ? KERNEL_USB_DEVICE_STATE_NEEDS_COMPANION :
                         (ready_for_enum ? KERNEL_USB_DEVICE_STATE_READY_FOR_ENUM :
                          KERNEL_USB_DEVICE_STATE_ATTACHED)));
        if (port_status->connected) {
            device->flags |= KERNEL_USB_DEVICE_FLAG_CONNECTED;
        }
        if (port_status->enabled) {
            device->flags |= KERNEL_USB_DEVICE_FLAG_ENABLED;
        }
        if (port_status->powered) {
            device->flags |= KERNEL_USB_DEVICE_FLAG_POWERED;
        }
        if (audio_candidate) {
            device->flags |= KERNEL_USB_DEVICE_FLAG_AUDIO_CANDIDATE;
        }
        if (ready_for_enum) {
            device->flags |= KERNEL_USB_DEVICE_FLAG_ENUM_READY;
            state->devices_ready_for_enum++;
        }
        if (control_ready) {
            device->flags |= KERNEL_USB_DEVICE_FLAG_CONTROL_READY;
            state->devices_control_ready++;
        }
        if (needs_companion) {
            device->flags |= KERNEL_USB_DEVICE_FLAG_NEEDS_COMPANION;
            state->devices_needing_companion++;
        }
        if (companion_controller_index != 0xffu) {
            device->flags |= KERNEL_USB_DEVICE_FLAG_COMPANION_PRESENT;
            state->devices_with_companion_present++;
        }
        if (handoff_ready) {
            device->flags |= KERNEL_USB_DEVICE_FLAG_HANDOFF_READY;
            state->devices_handoff_ready++;
        }
        if (control_path_ready) {
            device->flags |= KERNEL_USB_DEVICE_FLAG_CONTROL_PATH_READY;
            state->devices_control_path_ready++;
            state->device_probe_targets++;
            if (audio_candidate) {
                state->audio_probe_targets++;
            }
        }
        state->devices++;
    }

    if (audio_candidate) {
        state->audio_candidates++;
    }
}

static void kernel_usb_host_build_probe_snapshots(struct kernel_usb_host_state *state) {
    if (state == 0) {
        return;
    }

    state->probe_snapshots = 0u;
    state->audio_probe_snapshots = 0u;
    state->probe_dispatch_ready = 0u;
    state->audio_probe_dispatch_ready = 0u;
    state->probe_exec_ready = 0u;
    state->audio_probe_exec_ready = 0u;
    state->probe_descriptor_ready = 0u;
    state->audio_probe_descriptor_ready = 0u;
    state->probe_dispatch_cursor = 0u;
    state->audio_probe_dispatch_cursor = 0u;

    for (uint32_t i = 0u; i < state->devices && state->probe_snapshots < KERNEL_USB_MAX_PROBE_SNAPSHOTS; ++i) {
        struct kernel_usb_device_info *device = &state->device_entries[i];
        struct kernel_usb_probe_snapshot *snapshot;

        if ((device->flags & KERNEL_USB_DEVICE_FLAG_CONTROL_PATH_READY) == 0u) {
            continue;
        }

        snapshot = &state->probe_snapshot_entries[state->probe_snapshots];
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->device_index = i;
        snapshot->audio_candidate =
            (uint8_t)(((device->flags & KERNEL_USB_DEVICE_FLAG_AUDIO_CANDIDATE) != 0u) ? 1u : 0u);
        snapshot->status = KERNEL_USB_PROBE_STATUS_PLANNED;
        snapshot->plan.device = *device;
        snapshot->plan.request_type = (uint8_t)(USB_REQTYPE_IN |
                                                USB_REQTYPE_STANDARD |
                                                USB_REQTYPE_DEVICE);
        snapshot->plan.request = USB_REQUEST_GET_DESCRIPTOR;
        snapshot->plan.value = (uint16_t)(USB_DESCRIPTOR_TYPE_DEVICE << 8);
        snapshot->plan.index = 0u;
        snapshot->plan.length = USB_DEVICE_DESCRIPTOR_PROBE_LENGTH;
        if ((device->flags & KERNEL_USB_DEVICE_FLAG_CONTROL_PATH_READY) != 0u) {
            kernel_usb_host_probe_snapshot_set_status(state, snapshot, KERNEL_USB_PROBE_STATUS_DISPATCH_READY);
        } else {
            kernel_usb_host_probe_snapshot_set_status(state, snapshot, KERNEL_USB_PROBE_STATUS_DEFERRED_NO_TRANSPORT);
        }
        state->probe_snapshots++;
        if (snapshot->audio_candidate) {
            state->audio_probe_snapshots++;
        }
    }
}

static uint32_t kernel_usb_host_read32(uint32_t base, uint32_t reg) {
    volatile uint32_t *ptr = (volatile uint32_t *)(uintptr_t)(base + reg);
    return *ptr;
}

static void kernel_usb_host_write32(uint32_t base, uint32_t reg, uint32_t value) {
    volatile uint32_t *ptr = (volatile uint32_t *)(uintptr_t)(base + reg);
    *ptr = value;
}

static uint16_t kernel_usb_host_read16_io(uint32_t base, uint32_t reg) {
    return inw((uint16_t)(base + reg));
}

static void kernel_usb_host_write16_io(uint32_t base, uint32_t reg, uint16_t value) {
    outw((uint16_t)(base + reg), value);
}

static void kernel_usb_host_probe_status_apply_counters(struct kernel_usb_host_state *state,
                                                        uint8_t audio_candidate,
                                                        uint8_t old_status,
                                                        uint8_t new_status) {
    if (state == 0 || old_status == new_status) {
        return;
    }

    if (old_status == KERNEL_USB_PROBE_STATUS_DISPATCH_READY) {
        if (state->probe_dispatch_ready != 0u) {
            state->probe_dispatch_ready--;
        }
        if (audio_candidate && state->audio_probe_dispatch_ready != 0u) {
            state->audio_probe_dispatch_ready--;
        }
    } else if (old_status == KERNEL_USB_PROBE_STATUS_EXEC_READY) {
        if (state->probe_exec_ready != 0u) {
            state->probe_exec_ready--;
        }
        if (audio_candidate && state->audio_probe_exec_ready != 0u) {
            state->audio_probe_exec_ready--;
        }
    } else if (old_status == KERNEL_USB_PROBE_STATUS_DESCRIPTOR_READY) {
        if (state->probe_descriptor_ready != 0u) {
            state->probe_descriptor_ready--;
        }
        if (audio_candidate && state->audio_probe_descriptor_ready != 0u) {
            state->audio_probe_descriptor_ready--;
        }
    }

    if (new_status == KERNEL_USB_PROBE_STATUS_DISPATCH_READY) {
        state->probe_dispatch_ready++;
        if (audio_candidate) {
            state->audio_probe_dispatch_ready++;
        }
    } else if (new_status == KERNEL_USB_PROBE_STATUS_EXEC_READY) {
        state->probe_exec_ready++;
        if (audio_candidate) {
            state->audio_probe_exec_ready++;
        }
    } else if (new_status == KERNEL_USB_PROBE_STATUS_DESCRIPTOR_READY) {
        state->probe_descriptor_ready++;
        if (audio_candidate) {
            state->audio_probe_descriptor_ready++;
        }
    }
}

static void kernel_usb_host_probe_snapshot_set_status(struct kernel_usb_host_state *state,
                                                      struct kernel_usb_probe_snapshot *snapshot,
                                                      uint8_t new_status) {
    uint8_t audio_candidate;
    uint8_t old_status;

    if (state == 0 || snapshot == 0) {
        return;
    }

    audio_candidate = snapshot->audio_candidate;
    old_status = snapshot->status;
    snapshot->status = new_status;
    kernel_usb_host_probe_status_apply_counters(state, audio_candidate, old_status, new_status);
}

static int kernel_usb_host_probe_port_ready(const struct kernel_usb_probe_dispatch_context *dispatch) {
    const struct kernel_usb_host_port_status *status;

    if (dispatch == 0) {
        return -1;
    }

    status = &dispatch->effective_port_status;
    if (!status->connected || !status->powered || status->reset) {
        return -1;
    }

    return 0;
}

static void kernel_usb_host_probe_copy_descriptor_prefix(struct kernel_usb_probe_execution *execution,
                                                         struct kernel_usb_probe_snapshot *snapshot,
                                                         const uint8_t *descriptor_source,
                                                         uint32_t actual_length) {
    uint32_t copy_length;

    if (execution == 0) {
        return;
    }

    memset(execution->descriptor_prefix, 0, sizeof(execution->descriptor_prefix));
    execution->actual_length = 0u;

    copy_length = actual_length;
    if (copy_length > USB_DEVICE_DESCRIPTOR_FULL_LENGTH) {
        copy_length = USB_DEVICE_DESCRIPTOR_FULL_LENGTH;
    }

    if (copy_length != 0u && descriptor_source != 0) {
        memcpy(execution->descriptor_prefix, descriptor_source, copy_length);
        execution->actual_length = (uint8_t)copy_length;
    }

    if (snapshot != 0) {
        memset(snapshot->descriptor_prefix, 0, sizeof(snapshot->descriptor_prefix));
        snapshot->actual_length = execution->actual_length;
        if (execution->actual_length != 0u) {
            memcpy(snapshot->descriptor_prefix,
                   execution->descriptor_prefix,
                   execution->actual_length);
        }
    }
}

static int kernel_usb_host_validate_device_descriptor_prefix(uint8_t speed,
                                                             const uint8_t *descriptor,
                                                             uint32_t actual_length,
                                                             uint16_t *max_packet_size0_out) {
    uint16_t max_packet_size0;

    if (descriptor == 0 || actual_length < USB_DEVICE_DESCRIPTOR_PROBE_LENGTH) {
        return -1;
    }
    if (descriptor[0] < USB_DEVICE_DESCRIPTOR_FULL_LENGTH) {
        return -1;
    }
    if (descriptor[1] != USB_DESCRIPTOR_TYPE_DEVICE) {
        return -1;
    }

    max_packet_size0 = descriptor[7];
    if (max_packet_size0 == 0u) {
        return -1;
    }

    if (speed == KERNEL_USB_HOST_PORT_SPEED_LOW) {
        if (max_packet_size0 != 8u) {
            return -1;
        }
    } else {
        if (max_packet_size0 != 8u &&
            max_packet_size0 != 16u &&
            max_packet_size0 != 32u &&
            max_packet_size0 != 64u) {
            return -1;
        }
    }

    if (max_packet_size0_out != 0) {
        *max_packet_size0_out = max_packet_size0;
    }
    return 0;
}

static int kernel_usb_host_validate_full_device_descriptor(const uint8_t *descriptor,
                                                           uint32_t actual_length) {
    if (descriptor == 0 || actual_length < USB_DEVICE_DESCRIPTOR_FULL_LENGTH) {
        return -1;
    }
    if (descriptor[0] != USB_DEVICE_DESCRIPTOR_FULL_LENGTH) {
        return -1;
    }
    if (descriptor[1] != USB_DESCRIPTOR_TYPE_DEVICE) {
        return -1;
    }
    if (descriptor[17] == 0u) {
        return -1;
    }
    return 0;
}

static int kernel_usb_host_validate_assigned_address(uint8_t address) {
    if (address == 0u || address >= 128u) {
        return -1;
    }
    return 0;
}

static int kernel_usb_host_validate_config_descriptor_prefix(const uint8_t *descriptor,
                                                             uint32_t actual_length,
                                                             uint16_t *total_length_out,
                                                             uint8_t *configuration_value_out) {
    uint16_t total_length;

    if (descriptor == 0 || actual_length < USB_CONFIG_DESCRIPTOR_SHORT_LENGTH) {
        return -1;
    }
    if (descriptor[0] != USB_CONFIG_DESCRIPTOR_SHORT_LENGTH) {
        return -1;
    }
    if (descriptor[1] != USB_DESCRIPTOR_TYPE_CONFIG) {
        return -1;
    }

    total_length = (uint16_t)((uint16_t)descriptor[2] | ((uint16_t)descriptor[3] << 8));
    if (total_length < USB_CONFIG_DESCRIPTOR_SHORT_LENGTH) {
        return -1;
    }
    if (descriptor[4] == 0u || descriptor[5] == 0u) {
        return -1;
    }

    if (total_length_out != 0) {
        *total_length_out = total_length;
    }
    if (configuration_value_out != 0) {
        *configuration_value_out = descriptor[5];
    }
    return 0;
}

static int kernel_usb_host_validate_full_config_descriptor(const uint8_t *descriptor,
                                                           uint32_t actual_length,
                                                           uint16_t expected_total_length) {
    uint16_t total_length;

    if (descriptor == 0 || actual_length < USB_CONFIG_DESCRIPTOR_SHORT_LENGTH) {
        return -1;
    }
    if (descriptor[0] != USB_CONFIG_DESCRIPTOR_SHORT_LENGTH) {
        return -1;
    }
    if (descriptor[1] != USB_DESCRIPTOR_TYPE_CONFIG) {
        return -1;
    }
    total_length = (uint16_t)((uint16_t)descriptor[2] | ((uint16_t)descriptor[3] << 8));
    if (total_length != expected_total_length || actual_length < total_length) {
        return -1;
    }
    if (descriptor[4] == 0u || descriptor[5] == 0u) {
        return -1;
    }
    return 0;
}

static void kernel_usb_host_scan_config_descriptor(const uint8_t *descriptor,
                                                   uint32_t actual_length,
                                                   struct kernel_usb_probe_execution *execution) {
    uint32_t offset;

    if (descriptor == 0 || execution == 0) {
        return;
    }

    execution->audio_class_detected = 0u;
    execution->interface_count = 0u;
    execution->endpoint_count = 0u;

    for (offset = 0u; offset + 1u < actual_length;) {
        uint8_t length = descriptor[offset];
        uint8_t type = descriptor[offset + 1u];

        if (length == 0u || offset + length > actual_length) {
            break;
        }

        if (type == USB_DESCRIPTOR_TYPE_INTERFACE && length >= 9u) {
            execution->interface_count++;
            if (descriptor[offset + 5u] == USB_CLASS_AUDIO &&
                (descriptor[offset + 6u] == USB_SUBCLASS_AUDIOCONTROL ||
                 descriptor[offset + 6u] == USB_SUBCLASS_AUDIOSTREAM)) {
                execution->audio_class_detected = 1u;
            }
        } else if (type == USB_DESCRIPTOR_TYPE_ENDPOINT && length >= 7u) {
            execution->endpoint_count++;
        }

        offset += length;
    }
}

static int kernel_usb_host_probe_execute_uhci_transfer(const struct kernel_usb_probe_dispatch_context *dispatch,
                                                       uint8_t target_address,
                                                       uint8_t request_type,
                                                       uint8_t request,
                                                       uint16_t request_value,
                                                       uint16_t request_index,
                                                       uint16_t request_length,
                                                       uint8_t *data_buffer,
                                                       uint32_t data_capacity,
                                                       uint32_t *actual_length_out) {
    const struct kernel_usb_host_controller_info *controller;
    uint16_t saved_cmd;
    uint16_t saved_sts;
    uint16_t saved_intr;
    uint16_t saved_frnum;
    uint32_t saved_flbase;
    uint32_t setup_status;
    uint32_t data_status;
    uint32_t status_status;
    uint32_t actual_length;
    uint32_t qh_phys;
    uint32_t setup_phys;
    uint32_t data_phys;
    uint32_t status_phys;

    if (dispatch == 0 || data_buffer == 0 || actual_length_out == 0 ||
        request_length == 0u || request_length > data_capacity) {
        return -1;
    }
    if (kernel_usb_host_probe_execute_uhci(dispatch) != 0) {
        return -1;
    }

    controller = &dispatch->effective_controller;
    saved_cmd = kernel_usb_host_read16_io(controller->bar_base, UHCI_CMD);
    saved_sts = kernel_usb_host_read16_io(controller->bar_base, UHCI_STS);
    saved_intr = kernel_usb_host_read16_io(controller->bar_base, UHCI_INTR);
    saved_frnum = kernel_usb_host_read16_io(controller->bar_base, UHCI_FRNUM);
    saved_flbase = inl((uint16_t)(controller->bar_base + UHCI_FLBASEADDR));

    memset(g_kernel_usb_uhci_probe_frame_list, 0, sizeof(g_kernel_usb_uhci_probe_frame_list));
    memset(&g_kernel_usb_uhci_probe_qh, 0, sizeof(g_kernel_usb_uhci_probe_qh));
    memset(&g_kernel_usb_uhci_probe_setup_td, 0, sizeof(g_kernel_usb_uhci_probe_setup_td));
    memset(&g_kernel_usb_uhci_probe_data_td, 0, sizeof(g_kernel_usb_uhci_probe_data_td));
    memset(&g_kernel_usb_uhci_probe_status_td, 0, sizeof(g_kernel_usb_uhci_probe_status_td));
    memset(&g_kernel_usb_uhci_probe_request, 0, sizeof(g_kernel_usb_uhci_probe_request));
    memset(data_buffer, 0, data_capacity);

    g_kernel_usb_uhci_probe_request.request_type = request_type;
    g_kernel_usb_uhci_probe_request.request = request;
    g_kernel_usb_uhci_probe_request.value = request_value;
    g_kernel_usb_uhci_probe_request.index = request_index;
    g_kernel_usb_uhci_probe_request.length = request_length;

    qh_phys = (uint32_t)(uintptr_t)&g_kernel_usb_uhci_probe_qh;
    setup_phys = (uint32_t)(uintptr_t)&g_kernel_usb_uhci_probe_setup_td;
    data_phys = (uint32_t)(uintptr_t)&g_kernel_usb_uhci_probe_data_td;
    status_phys = (uint32_t)(uintptr_t)&g_kernel_usb_uhci_probe_status_td;

    g_kernel_usb_uhci_probe_qh.qh_hlink = UHCI_PTR_T;
    g_kernel_usb_uhci_probe_qh.qh_elink = setup_phys | UHCI_PTR_VF | UHCI_PTR_TD;

    g_kernel_usb_uhci_probe_setup_td.td_link = data_phys | UHCI_PTR_VF | UHCI_PTR_TD;
    g_kernel_usb_uhci_probe_setup_td.td_status = KERNEL_USB_UHCI_TD_ERRCNT(3) |
                                                 ((dispatch->snapshot.plan.device.speed == KERNEL_USB_HOST_PORT_SPEED_LOW) ?
                                                    UHCI_TD_LS : 0u) |
                                                 UHCI_TD_ACTIVE;
    g_kernel_usb_uhci_probe_setup_td.td_token = KERNEL_USB_UHCI_TD_SETUP(USB_SETUP_PACKET_LENGTH, 0u, target_address);
    g_kernel_usb_uhci_probe_setup_td.td_buffer = (uint32_t)(uintptr_t)&g_kernel_usb_uhci_probe_request;

    g_kernel_usb_uhci_probe_data_td.td_link = status_phys | UHCI_PTR_VF | UHCI_PTR_TD;
    g_kernel_usb_uhci_probe_data_td.td_status = KERNEL_USB_UHCI_TD_ERRCNT(3) |
                                                ((dispatch->snapshot.plan.device.speed == KERNEL_USB_HOST_PORT_SPEED_LOW) ?
                                                    UHCI_TD_LS : 0u) |
                                                UHCI_TD_ACTIVE;
    g_kernel_usb_uhci_probe_data_td.td_token = KERNEL_USB_UHCI_TD_IN(request_length, 0u, target_address, 1u);
    g_kernel_usb_uhci_probe_data_td.td_buffer = (uint32_t)(uintptr_t)data_buffer;

    g_kernel_usb_uhci_probe_status_td.td_link = UHCI_PTR_T;
    g_kernel_usb_uhci_probe_status_td.td_status = KERNEL_USB_UHCI_TD_ERRCNT(3) |
                                                  ((dispatch->snapshot.plan.device.speed == KERNEL_USB_HOST_PORT_SPEED_LOW) ?
                                                    UHCI_TD_LS : 0u) |
                                                  UHCI_TD_ACTIVE |
                                                  UHCI_TD_IOC;
    g_kernel_usb_uhci_probe_status_td.td_token = KERNEL_USB_UHCI_TD_OUT(0u, 0u, target_address, 1u);
    g_kernel_usb_uhci_probe_status_td.td_buffer = 0u;

    for (uint32_t i = 0u; i < UHCI_FRAMELIST_COUNT; ++i) {
        g_kernel_usb_uhci_probe_frame_list[i] = qh_phys | UHCI_PTR_QH;
    }

    kernel_usb_host_write16_io(controller->bar_base, UHCI_INTR, 0u);
    kernel_usb_host_write16_io(controller->bar_base, UHCI_STS, UHCI_STS_ALLINTRS);
    kernel_usb_host_write16_io(controller->bar_base, UHCI_FRNUM, 0u);
    outl((uint16_t)(controller->bar_base + UHCI_FLBASEADDR),
         (uint32_t)(uintptr_t)&g_kernel_usb_uhci_probe_frame_list[0]);
    kernel_usb_host_write16_io(controller->bar_base, UHCI_CMD, (uint16_t)(UHCI_CMD_CF | UHCI_CMD_RS));

    for (uint32_t spin = 0u; spin < KERNEL_USB_UHCI_POLL_LIMIT; ++spin) {
        if ((g_kernel_usb_uhci_probe_status_td.td_status & UHCI_TD_ACTIVE) == 0u) {
            break;
        }
        io_wait();
    }

    kernel_usb_host_write16_io(controller->bar_base, UHCI_CMD, saved_cmd);
    outl((uint16_t)(controller->bar_base + UHCI_FLBASEADDR), saved_flbase);
    kernel_usb_host_write16_io(controller->bar_base, UHCI_FRNUM, saved_frnum);
    kernel_usb_host_write16_io(controller->bar_base, UHCI_INTR, saved_intr);
    kernel_usb_host_write16_io(controller->bar_base, UHCI_STS, saved_sts & UHCI_STS_ALLINTRS);

    setup_status = g_kernel_usb_uhci_probe_setup_td.td_status;
    data_status = g_kernel_usb_uhci_probe_data_td.td_status;
    status_status = g_kernel_usb_uhci_probe_status_td.td_status;

    if ((status_status & UHCI_TD_ACTIVE) != 0u) {
        return 1;
    }
    if ((setup_status & 0x007e0000u) != 0u ||
        (data_status & 0x007e0000u) != 0u ||
        (status_status & 0x007e0000u) != 0u) {
        return -1;
    }

    actual_length = KERNEL_USB_UHCI_TD_GET_ACTLEN(data_status);
    if (actual_length > request_length) {
        actual_length = request_length;
    }
    *actual_length_out = actual_length;
    return 0;
}

static int kernel_usb_host_probe_execute_uhci_set_address(const struct kernel_usb_probe_dispatch_context *dispatch,
                                                          uint8_t address) {
    const struct kernel_usb_host_controller_info *controller;
    uint16_t saved_cmd;
    uint16_t saved_sts;
    uint16_t saved_intr;
    uint16_t saved_frnum;
    uint32_t saved_flbase;
    uint32_t setup_status;
    uint32_t status_status;
    uint32_t qh_phys;
    uint32_t setup_phys;
    uint32_t status_phys;

    if (dispatch == 0 || kernel_usb_host_validate_assigned_address(address) != 0) {
        return -1;
    }
    if (kernel_usb_host_probe_execute_uhci(dispatch) != 0) {
        return -1;
    }

    controller = &dispatch->effective_controller;
    saved_cmd = kernel_usb_host_read16_io(controller->bar_base, UHCI_CMD);
    saved_sts = kernel_usb_host_read16_io(controller->bar_base, UHCI_STS);
    saved_intr = kernel_usb_host_read16_io(controller->bar_base, UHCI_INTR);
    saved_frnum = kernel_usb_host_read16_io(controller->bar_base, UHCI_FRNUM);
    saved_flbase = inl((uint16_t)(controller->bar_base + UHCI_FLBASEADDR));

    memset(g_kernel_usb_uhci_probe_frame_list, 0, sizeof(g_kernel_usb_uhci_probe_frame_list));
    memset(&g_kernel_usb_uhci_probe_qh, 0, sizeof(g_kernel_usb_uhci_probe_qh));
    memset(&g_kernel_usb_uhci_probe_setup_td, 0, sizeof(g_kernel_usb_uhci_probe_setup_td));
    memset(&g_kernel_usb_uhci_probe_status_td, 0, sizeof(g_kernel_usb_uhci_probe_status_td));
    memset(&g_kernel_usb_uhci_probe_request, 0, sizeof(g_kernel_usb_uhci_probe_request));

    g_kernel_usb_uhci_probe_request.request_type = 0u;
    g_kernel_usb_uhci_probe_request.request = 0x05u;
    g_kernel_usb_uhci_probe_request.value = address;
    g_kernel_usb_uhci_probe_request.index = 0u;
    g_kernel_usb_uhci_probe_request.length = 0u;

    qh_phys = (uint32_t)(uintptr_t)&g_kernel_usb_uhci_probe_qh;
    setup_phys = (uint32_t)(uintptr_t)&g_kernel_usb_uhci_probe_setup_td;
    status_phys = (uint32_t)(uintptr_t)&g_kernel_usb_uhci_probe_status_td;

    g_kernel_usb_uhci_probe_qh.qh_hlink = UHCI_PTR_T;
    g_kernel_usb_uhci_probe_qh.qh_elink = setup_phys | UHCI_PTR_VF | UHCI_PTR_TD;

    g_kernel_usb_uhci_probe_setup_td.td_link = status_phys | UHCI_PTR_VF | UHCI_PTR_TD;
    g_kernel_usb_uhci_probe_setup_td.td_status = KERNEL_USB_UHCI_TD_ERRCNT(3) |
                                                 ((dispatch->snapshot.plan.device.speed == KERNEL_USB_HOST_PORT_SPEED_LOW) ?
                                                    UHCI_TD_LS : 0u) |
                                                 UHCI_TD_ACTIVE;
    g_kernel_usb_uhci_probe_setup_td.td_token = KERNEL_USB_UHCI_TD_SETUP(USB_SETUP_PACKET_LENGTH, 0u, 0u);
    g_kernel_usb_uhci_probe_setup_td.td_buffer = (uint32_t)(uintptr_t)&g_kernel_usb_uhci_probe_request;

    g_kernel_usb_uhci_probe_status_td.td_link = UHCI_PTR_T;
    g_kernel_usb_uhci_probe_status_td.td_status = KERNEL_USB_UHCI_TD_ERRCNT(3) |
                                                  ((dispatch->snapshot.plan.device.speed == KERNEL_USB_HOST_PORT_SPEED_LOW) ?
                                                    UHCI_TD_LS : 0u) |
                                                  UHCI_TD_ACTIVE |
                                                  UHCI_TD_IOC;
    g_kernel_usb_uhci_probe_status_td.td_token = KERNEL_USB_UHCI_TD_IN(0u, 0u, 0u, 1u);
    g_kernel_usb_uhci_probe_status_td.td_buffer = 0u;

    for (uint32_t i = 0u; i < UHCI_FRAMELIST_COUNT; ++i) {
        g_kernel_usb_uhci_probe_frame_list[i] = qh_phys | UHCI_PTR_QH;
    }

    kernel_usb_host_write16_io(controller->bar_base, UHCI_INTR, 0u);
    kernel_usb_host_write16_io(controller->bar_base, UHCI_STS, UHCI_STS_ALLINTRS);
    kernel_usb_host_write16_io(controller->bar_base, UHCI_FRNUM, 0u);
    outl((uint16_t)(controller->bar_base + UHCI_FLBASEADDR),
         (uint32_t)(uintptr_t)&g_kernel_usb_uhci_probe_frame_list[0]);
    kernel_usb_host_write16_io(controller->bar_base, UHCI_CMD, (uint16_t)(UHCI_CMD_CF | UHCI_CMD_RS));

    for (uint32_t spin = 0u; spin < KERNEL_USB_UHCI_POLL_LIMIT; ++spin) {
        if ((g_kernel_usb_uhci_probe_status_td.td_status & UHCI_TD_ACTIVE) == 0u) {
            break;
        }
        io_wait();
    }
    for (uint32_t settle = 0u; settle < 1024u; ++settle) {
        io_wait();
    }

    kernel_usb_host_write16_io(controller->bar_base, UHCI_CMD, saved_cmd);
    outl((uint16_t)(controller->bar_base + UHCI_FLBASEADDR), saved_flbase);
    kernel_usb_host_write16_io(controller->bar_base, UHCI_FRNUM, saved_frnum);
    kernel_usb_host_write16_io(controller->bar_base, UHCI_INTR, saved_intr);
    kernel_usb_host_write16_io(controller->bar_base, UHCI_STS, saved_sts & UHCI_STS_ALLINTRS);

    setup_status = g_kernel_usb_uhci_probe_setup_td.td_status;
    status_status = g_kernel_usb_uhci_probe_status_td.td_status;
    if ((status_status & UHCI_TD_ACTIVE) != 0u) {
        return 1;
    }
    if ((setup_status & 0x007e0000u) != 0u || (status_status & 0x007e0000u) != 0u) {
        return -1;
    }
    return 0;
}

static int kernel_usb_host_probe_execute_ohci_transfer(const struct kernel_usb_probe_dispatch_context *dispatch,
                                                       uint8_t target_address,
                                                       uint16_t request_length,
                                                       uint16_t max_packet_size0,
                                                       uint8_t request_type,
                                                       uint8_t request,
                                                       uint16_t request_value,
                                                       uint16_t request_index,
                                                       uint8_t *data_buffer,
                                                       uint32_t data_capacity,
                                                       uint32_t *actual_length_out) {
    const struct kernel_usb_host_controller_info *controller;
    uint32_t saved_control;
    uint32_t saved_intr_status;
    uint32_t saved_hcca;
    uint32_t saved_head;
    uint32_t saved_current;
    uint32_t actual_length;

    if (dispatch == 0 || data_buffer == 0 || actual_length_out == 0 ||
        request_length == 0u || request_length > data_capacity || max_packet_size0 == 0u) {
        return -1;
    }
    if (kernel_usb_host_probe_execute_ohci(dispatch) != 0) {
        return -1;
    }

    controller = &dispatch->effective_controller;
    saved_control = kernel_usb_host_read32(controller->bar_base, OHCI_CONTROL);
    saved_intr_status = kernel_usb_host_read32(controller->bar_base, OHCI_INTERRUPT_STATUS);
    saved_hcca = kernel_usb_host_read32(controller->bar_base, OHCI_HCCA);
    saved_head = kernel_usb_host_read32(controller->bar_base, OHCI_CONTROL_HEAD_ED);
    saved_current = kernel_usb_host_read32(controller->bar_base, OHCI_CONTROL_CURRENT_ED);

    memset(&g_kernel_usb_ohci_probe_hcca, 0, sizeof(g_kernel_usb_ohci_probe_hcca));
    memset(&g_kernel_usb_ohci_probe_ed, 0, sizeof(g_kernel_usb_ohci_probe_ed));
    memset(&g_kernel_usb_ohci_probe_setup_td, 0, sizeof(g_kernel_usb_ohci_probe_setup_td));
    memset(&g_kernel_usb_ohci_probe_data_td, 0, sizeof(g_kernel_usb_ohci_probe_data_td));
    memset(&g_kernel_usb_ohci_probe_status_td, 0, sizeof(g_kernel_usb_ohci_probe_status_td));
    memset(&g_kernel_usb_ohci_probe_tail_td, 0, sizeof(g_kernel_usb_ohci_probe_tail_td));
    memset(&g_kernel_usb_ohci_probe_request, 0, sizeof(g_kernel_usb_ohci_probe_request));
    memset(data_buffer, 0, data_capacity);

    g_kernel_usb_ohci_probe_request.request_type = request_type;
    g_kernel_usb_ohci_probe_request.request = request;
    g_kernel_usb_ohci_probe_request.value = request_value;
    g_kernel_usb_ohci_probe_request.index = request_index;
    g_kernel_usb_ohci_probe_request.length = request_length;

    g_kernel_usb_ohci_probe_ed.ed_flags = OHCI_ED_SET_FA(target_address) |
                                          OHCI_ED_SET_EN(0u) |
                                          ((dispatch->snapshot.plan.device.speed == KERNEL_USB_HOST_PORT_SPEED_LOW) ?
                                            OHCI_ED_SPEED : 0u) |
                                          OHCI_ED_SET_MAXP(max_packet_size0);
    g_kernel_usb_ohci_probe_ed.ed_tailp = (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_tail_td;
    g_kernel_usb_ohci_probe_ed.ed_headp = (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_setup_td;
    g_kernel_usb_ohci_probe_ed.ed_nexted = 0u;

    g_kernel_usb_ohci_probe_setup_td.td_flags = OHCI_TD_SETUP | OHCI_TD_NOCC | OHCI_TD_TOGGLE_0;
    g_kernel_usb_ohci_probe_setup_td.td_cbp = (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_request;
    g_kernel_usb_ohci_probe_setup_td.td_nexttd = (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_data_td;
    g_kernel_usb_ohci_probe_setup_td.td_be =
        g_kernel_usb_ohci_probe_setup_td.td_cbp + (uint32_t)sizeof(g_kernel_usb_ohci_probe_request) - 1u;

    g_kernel_usb_ohci_probe_data_td.td_flags = OHCI_TD_IN | OHCI_TD_NOCC | OHCI_TD_TOGGLE_1;
    g_kernel_usb_ohci_probe_data_td.td_cbp = (uint32_t)(uintptr_t)data_buffer;
    g_kernel_usb_ohci_probe_data_td.td_nexttd = (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_status_td;
    g_kernel_usb_ohci_probe_data_td.td_be = g_kernel_usb_ohci_probe_data_td.td_cbp + request_length - 1u;

    g_kernel_usb_ohci_probe_status_td.td_flags = OHCI_TD_OUT | OHCI_TD_NOCC | OHCI_TD_TOGGLE_1 | OHCI_TD_SET_DI(1u);
    g_kernel_usb_ohci_probe_status_td.td_cbp = 0u;
    g_kernel_usb_ohci_probe_status_td.td_nexttd = (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_tail_td;
    g_kernel_usb_ohci_probe_status_td.td_be = 0u;

    kernel_usb_host_write32(controller->bar_base, OHCI_INTERRUPT_DISABLE, (uint32_t)(OHCI_MIE | OHCI_ALL_INTRS));
    kernel_usb_host_write32(controller->bar_base, OHCI_INTERRUPT_STATUS, OHCI_ALL_INTRS);
    kernel_usb_host_write32(controller->bar_base,
                            OHCI_HCCA,
                            (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_hcca);
    kernel_usb_host_write32(controller->bar_base,
                            OHCI_CONTROL_HEAD_ED,
                            (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_ed);
    kernel_usb_host_write32(controller->bar_base, OHCI_CONTROL_CURRENT_ED, 0u);
    kernel_usb_host_write32(controller->bar_base,
                            OHCI_CONTROL,
                            (saved_control & ~OHCI_HCFS_MASK) | OHCI_HCFS_OPERATIONAL | OHCI_CLE);
    kernel_usb_host_write32(controller->bar_base, OHCI_COMMAND_STATUS, OHCI_CLF);

    for (uint32_t spin = 0u; spin < KERNEL_USB_UHCI_POLL_LIMIT; ++spin) {
        if (OHCI_TD_GET_CC(g_kernel_usb_ohci_probe_status_td.td_flags) != 0x0fu) {
            break;
        }
        io_wait();
    }

    kernel_usb_host_write32(controller->bar_base, OHCI_CONTROL, saved_control);
    kernel_usb_host_write32(controller->bar_base, OHCI_CONTROL_HEAD_ED, saved_head);
    kernel_usb_host_write32(controller->bar_base, OHCI_CONTROL_CURRENT_ED, saved_current);
    kernel_usb_host_write32(controller->bar_base, OHCI_HCCA, saved_hcca);
    kernel_usb_host_write32(controller->bar_base, OHCI_INTERRUPT_STATUS, saved_intr_status & OHCI_ALL_INTRS);

    if (OHCI_TD_GET_CC(g_kernel_usb_ohci_probe_status_td.td_flags) == 0x0fu) {
        return 1;
    }
    if (OHCI_TD_GET_CC(g_kernel_usb_ohci_probe_setup_td.td_flags) != 0u ||
        OHCI_TD_GET_CC(g_kernel_usb_ohci_probe_data_td.td_flags) != 0u ||
        OHCI_TD_GET_CC(g_kernel_usb_ohci_probe_status_td.td_flags) != 0u) {
        return -1;
    }

    actual_length = request_length;
    if (g_kernel_usb_ohci_probe_data_td.td_cbp != 0u &&
        g_kernel_usb_ohci_probe_data_td.td_be >= g_kernel_usb_ohci_probe_data_td.td_cbp) {
        uint32_t residual = (g_kernel_usb_ohci_probe_data_td.td_be -
                             g_kernel_usb_ohci_probe_data_td.td_cbp) + 1u;
        if (residual < actual_length) {
            actual_length -= residual;
        }
    }
    *actual_length_out = actual_length;
    return 0;
}

static int kernel_usb_host_probe_execute_ohci_set_address(const struct kernel_usb_probe_dispatch_context *dispatch,
                                                          uint8_t address) {
    const struct kernel_usb_host_controller_info *controller;
    uint32_t saved_control;
    uint32_t saved_intr_status;
    uint32_t saved_hcca;
    uint32_t saved_head;
    uint32_t saved_current;

    if (dispatch == 0 || kernel_usb_host_validate_assigned_address(address) != 0) {
        return -1;
    }
    if (kernel_usb_host_probe_execute_ohci(dispatch) != 0) {
        return -1;
    }

    controller = &dispatch->effective_controller;
    saved_control = kernel_usb_host_read32(controller->bar_base, OHCI_CONTROL);
    saved_intr_status = kernel_usb_host_read32(controller->bar_base, OHCI_INTERRUPT_STATUS);
    saved_hcca = kernel_usb_host_read32(controller->bar_base, OHCI_HCCA);
    saved_head = kernel_usb_host_read32(controller->bar_base, OHCI_CONTROL_HEAD_ED);
    saved_current = kernel_usb_host_read32(controller->bar_base, OHCI_CONTROL_CURRENT_ED);

    memset(&g_kernel_usb_ohci_probe_hcca, 0, sizeof(g_kernel_usb_ohci_probe_hcca));
    memset(&g_kernel_usb_ohci_probe_ed, 0, sizeof(g_kernel_usb_ohci_probe_ed));
    memset(&g_kernel_usb_ohci_probe_setup_td, 0, sizeof(g_kernel_usb_ohci_probe_setup_td));
    memset(&g_kernel_usb_ohci_probe_status_td, 0, sizeof(g_kernel_usb_ohci_probe_status_td));
    memset(&g_kernel_usb_ohci_probe_tail_td, 0, sizeof(g_kernel_usb_ohci_probe_tail_td));
    memset(&g_kernel_usb_ohci_probe_request, 0, sizeof(g_kernel_usb_ohci_probe_request));

    g_kernel_usb_ohci_probe_request.request_type = 0u;
    g_kernel_usb_ohci_probe_request.request = 0x05u;
    g_kernel_usb_ohci_probe_request.value = address;
    g_kernel_usb_ohci_probe_request.index = 0u;
    g_kernel_usb_ohci_probe_request.length = 0u;

    g_kernel_usb_ohci_probe_ed.ed_flags = OHCI_ED_SET_FA(0u) |
                                          OHCI_ED_SET_EN(0u) |
                                          ((dispatch->snapshot.plan.device.speed == KERNEL_USB_HOST_PORT_SPEED_LOW) ?
                                            OHCI_ED_SPEED : 0u) |
                                          OHCI_ED_SET_MAXP(8u);
    g_kernel_usb_ohci_probe_ed.ed_tailp = (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_tail_td;
    g_kernel_usb_ohci_probe_ed.ed_headp = (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_setup_td;
    g_kernel_usb_ohci_probe_ed.ed_nexted = 0u;

    g_kernel_usb_ohci_probe_setup_td.td_flags = OHCI_TD_SETUP | OHCI_TD_NOCC | OHCI_TD_TOGGLE_0;
    g_kernel_usb_ohci_probe_setup_td.td_cbp = (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_request;
    g_kernel_usb_ohci_probe_setup_td.td_nexttd = (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_status_td;
    g_kernel_usb_ohci_probe_setup_td.td_be =
        g_kernel_usb_ohci_probe_setup_td.td_cbp + (uint32_t)sizeof(g_kernel_usb_ohci_probe_request) - 1u;

    g_kernel_usb_ohci_probe_status_td.td_flags = OHCI_TD_IN | OHCI_TD_NOCC | OHCI_TD_TOGGLE_1 | OHCI_TD_SET_DI(1u);
    g_kernel_usb_ohci_probe_status_td.td_cbp = 0u;
    g_kernel_usb_ohci_probe_status_td.td_nexttd = (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_tail_td;
    g_kernel_usb_ohci_probe_status_td.td_be = 0u;

    kernel_usb_host_write32(controller->bar_base, OHCI_INTERRUPT_DISABLE, (uint32_t)(OHCI_MIE | OHCI_ALL_INTRS));
    kernel_usb_host_write32(controller->bar_base, OHCI_INTERRUPT_STATUS, OHCI_ALL_INTRS);
    kernel_usb_host_write32(controller->bar_base,
                            OHCI_HCCA,
                            (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_hcca);
    kernel_usb_host_write32(controller->bar_base,
                            OHCI_CONTROL_HEAD_ED,
                            (uint32_t)(uintptr_t)&g_kernel_usb_ohci_probe_ed);
    kernel_usb_host_write32(controller->bar_base, OHCI_CONTROL_CURRENT_ED, 0u);
    kernel_usb_host_write32(controller->bar_base,
                            OHCI_CONTROL,
                            (saved_control & ~OHCI_HCFS_MASK) | OHCI_HCFS_OPERATIONAL | OHCI_CLE);
    kernel_usb_host_write32(controller->bar_base, OHCI_COMMAND_STATUS, OHCI_CLF);

    for (uint32_t spin = 0u; spin < KERNEL_USB_UHCI_POLL_LIMIT; ++spin) {
        if (OHCI_TD_GET_CC(g_kernel_usb_ohci_probe_status_td.td_flags) != 0x0fu) {
            break;
        }
        io_wait();
    }
    for (uint32_t settle = 0u; settle < 1024u; ++settle) {
        io_wait();
    }

    kernel_usb_host_write32(controller->bar_base, OHCI_CONTROL, saved_control);
    kernel_usb_host_write32(controller->bar_base, OHCI_CONTROL_HEAD_ED, saved_head);
    kernel_usb_host_write32(controller->bar_base, OHCI_CONTROL_CURRENT_ED, saved_current);
    kernel_usb_host_write32(controller->bar_base, OHCI_HCCA, saved_hcca);
    kernel_usb_host_write32(controller->bar_base, OHCI_INTERRUPT_STATUS, saved_intr_status & OHCI_ALL_INTRS);

    if (OHCI_TD_GET_CC(g_kernel_usb_ohci_probe_status_td.td_flags) == 0x0fu) {
        return 1;
    }
    if (OHCI_TD_GET_CC(g_kernel_usb_ohci_probe_setup_td.td_flags) != 0u ||
        OHCI_TD_GET_CC(g_kernel_usb_ohci_probe_status_td.td_flags) != 0u) {
        return -1;
    }
    return 0;
}

static void kernel_usb_host_probe_copy_config_prefix(struct kernel_usb_probe_execution *execution,
                                                     struct kernel_usb_probe_snapshot *snapshot,
                                                     const uint8_t *descriptor_source,
                                                     uint32_t actual_length) {
    uint32_t copy_length;

    if (execution == 0) {
        return;
    }

    memset(execution->config_descriptor_prefix, 0, sizeof(execution->config_descriptor_prefix));
    execution->config_valid = 0u;
    execution->audio_class_detected = 0u;
    execution->config_total_length = 0u;
    execution->configuration_value = 0u;
    execution->interface_count = 0u;
    execution->endpoint_count = 0u;

    copy_length = actual_length;
    if (copy_length > USB_CONFIG_DESCRIPTOR_SHORT_LENGTH) {
        copy_length = USB_CONFIG_DESCRIPTOR_SHORT_LENGTH;
    }
    if (copy_length != 0u && descriptor_source != 0) {
        memcpy(execution->config_descriptor_prefix, descriptor_source, copy_length);
    }

    if (snapshot != 0) {
        memset(snapshot->config_descriptor_prefix, 0, sizeof(snapshot->config_descriptor_prefix));
        snapshot->config_valid = 0u;
        snapshot->audio_class_detected = 0u;
        snapshot->config_total_length = 0u;
        snapshot->configuration_value = 0u;
        snapshot->interface_count = 0u;
        snapshot->endpoint_count = 0u;
        if (copy_length != 0u && descriptor_source != 0) {
            memcpy(snapshot->config_descriptor_prefix, descriptor_source, copy_length);
        }
    }
}

static int kernel_usb_host_probe_execute_uhci(const struct kernel_usb_probe_dispatch_context *dispatch) {
    const struct kernel_usb_host_controller_info *controller;
    uint16_t cmd;
    uint16_t sts;
    uint16_t intr;
    uint16_t frnum;
    uint32_t flbase;

    if (dispatch == 0 || kernel_usb_host_probe_port_ready(dispatch) != 0) {
        return -1;
    }

    controller = &dispatch->effective_controller;
    if (controller->bar_base == 0u || controller->bar_is_mmio) {
        return -1;
    }

    cmd = kernel_usb_host_read16_io(controller->bar_base, UHCI_CMD);
    sts = kernel_usb_host_read16_io(controller->bar_base, UHCI_STS);
    intr = kernel_usb_host_read16_io(controller->bar_base, UHCI_INTR);
    frnum = kernel_usb_host_read16_io(controller->bar_base, UHCI_FRNUM);
    flbase = inl((uint16_t)(controller->bar_base + UHCI_FLBASEADDR));
    if (cmd == 0xffffu || sts == 0xffffu || intr == 0xffffu || frnum == 0xffffu || flbase == 0xffffffffu) {
        return -1;
    }

    kernel_usb_host_write16_io(controller->bar_base, UHCI_INTR, 0u);
    if ((sts & UHCI_STS_ALLINTRS) != 0u) {
        kernel_usb_host_write16_io(controller->bar_base, UHCI_STS, (uint16_t)(sts & UHCI_STS_ALLINTRS));
    }
    if ((cmd & UHCI_CMD_CF) == 0u) {
        kernel_usb_host_write16_io(controller->bar_base,
                                   UHCI_CMD,
                                   (uint16_t)((cmd & ~UHCI_CMD_RS) | UHCI_CMD_CF));
    }

    return 0;
}

static int kernel_usb_host_probe_execute_uhci_descriptor(const struct kernel_usb_probe_dispatch_context *dispatch,
                                                         struct kernel_usb_probe_execution *execution) {
    uint32_t actual_length;
    uint32_t config_full_length;
    uint16_t max_packet_size0;
    uint16_t config_total_length;
    uint8_t configuration_value;

    if (dispatch == 0 || execution == 0) {
        return -1;
    }
    if (kernel_usb_host_probe_execute_uhci_transfer(dispatch,
                                                    0u,
                                                    dispatch->snapshot.plan.request_type,
                                                    dispatch->snapshot.plan.request,
                                                    dispatch->snapshot.plan.value,
                                                    dispatch->snapshot.plan.index,
                                                    USB_DEVICE_DESCRIPTOR_PROBE_LENGTH,
                                                    &g_kernel_usb_uhci_probe_data[0],
                                                    sizeof(g_kernel_usb_uhci_probe_data),
                                                    &actual_length) != 0) {
        return -1;
    }
    kernel_usb_host_probe_copy_descriptor_prefix(execution,
                                                 0,
                                                 &g_kernel_usb_uhci_probe_data[0],
                                                 actual_length);
    if (kernel_usb_host_validate_device_descriptor_prefix(dispatch->snapshot.plan.device.speed,
                                                          execution->descriptor_prefix,
                                                          execution->actual_length,
                                                          &max_packet_size0) != 0) {
        execution->actual_length = 0u;
        memset(execution->descriptor_prefix, 0, sizeof(execution->descriptor_prefix));
        return -1;
    }
    execution->descriptor_valid = 1u;
    execution->max_packet_size0 = max_packet_size0;

    if (kernel_usb_host_probe_execute_uhci_transfer(dispatch,
                                                    0u,
                                                    dispatch->snapshot.plan.request_type,
                                                    dispatch->snapshot.plan.request,
                                                    dispatch->snapshot.plan.value,
                                                    dispatch->snapshot.plan.index,
                                                    USB_DEVICE_DESCRIPTOR_FULL_LENGTH,
                                                    &g_kernel_usb_uhci_probe_data[0],
                                                    sizeof(g_kernel_usb_uhci_probe_data),
                                                    &actual_length) != 0) {
        return -1;
    }
    kernel_usb_host_probe_copy_descriptor_prefix(execution,
                                                 0,
                                                 &g_kernel_usb_uhci_probe_data[0],
                                                 actual_length);
    if (kernel_usb_host_validate_full_device_descriptor(execution->descriptor_prefix,
                                                        execution->actual_length) != 0) {
        execution->actual_length = 0u;
        execution->descriptor_valid = 0u;
        execution->max_packet_size0 = 0u;
        memset(execution->descriptor_prefix, 0, sizeof(execution->descriptor_prefix));
        return -1;
    }
    execution->descriptor_valid = 1u;
    execution->max_packet_size0 = max_packet_size0;
    if (kernel_usb_host_probe_execute_uhci_set_address(dispatch,
                                                       dispatch->snapshot.plan.device.address) != 0) {
        return -1;
    }
    execution->assigned_address = dispatch->snapshot.plan.device.address;
    if (kernel_usb_host_probe_execute_uhci_transfer(dispatch,
                                                    execution->assigned_address,
                                                    (uint8_t)(USB_REQTYPE_IN |
                                                              USB_REQTYPE_STANDARD |
                                                              USB_REQTYPE_DEVICE),
                                                    USB_REQUEST_GET_DESCRIPTOR,
                                                    (uint16_t)(USB_DESCRIPTOR_TYPE_CONFIG << 8),
                                                    0u,
                                                    USB_CONFIG_DESCRIPTOR_SHORT_LENGTH,
                                                    &g_kernel_usb_config_probe_data[0],
                                                    sizeof(g_kernel_usb_config_probe_data),
                                                    &actual_length) != 0) {
        return -1;
    }
    kernel_usb_host_probe_copy_config_prefix(execution,
                                             0,
                                             &g_kernel_usb_config_probe_data[0],
                                             actual_length);
    if (kernel_usb_host_validate_config_descriptor_prefix(execution->config_descriptor_prefix,
                                                          actual_length,
                                                          &config_total_length,
                                                          &configuration_value) != 0) {
        return -1;
    }
    execution->config_valid = 1u;
    execution->config_total_length = config_total_length;
    execution->configuration_value = configuration_value;
    config_full_length = config_total_length;
    if (config_full_length > sizeof(g_kernel_usb_config_full_data)) {
        config_full_length = sizeof(g_kernel_usb_config_full_data);
    }
    if (kernel_usb_host_probe_execute_uhci_transfer(dispatch,
                                                    execution->assigned_address,
                                                    (uint8_t)(USB_REQTYPE_IN |
                                                              USB_REQTYPE_STANDARD |
                                                              USB_REQTYPE_DEVICE),
                                                    USB_REQUEST_GET_DESCRIPTOR,
                                                    (uint16_t)(USB_DESCRIPTOR_TYPE_CONFIG << 8),
                                                    0u,
                                                    (uint16_t)config_full_length,
                                                    &g_kernel_usb_config_full_data[0],
                                                    sizeof(g_kernel_usb_config_full_data),
                                                    &actual_length) != 0) {
        return -1;
    }
    if (kernel_usb_host_validate_full_config_descriptor(&g_kernel_usb_config_full_data[0],
                                                        actual_length,
                                                        config_total_length) != 0) {
        return -1;
    }
    kernel_usb_host_scan_config_descriptor(&g_kernel_usb_config_full_data[0], actual_length, execution);
    execution->config_valid = 1u;
    return 0;
}

static int kernel_usb_host_probe_execute_ohci(const struct kernel_usb_probe_dispatch_context *dispatch) {
    const struct kernel_usb_host_controller_info *controller;
    uint32_t revision;
    uint32_t control;
    uint32_t intr_status;

    if (dispatch == 0 || kernel_usb_host_probe_port_ready(dispatch) != 0) {
        return -1;
    }

    controller = &dispatch->effective_controller;
    if (controller->bar_base == 0u || !controller->bar_is_mmio) {
        return -1;
    }

    revision = kernel_usb_host_read32(controller->bar_base, OHCI_REVISION);
    control = kernel_usb_host_read32(controller->bar_base, OHCI_CONTROL);
    intr_status = kernel_usb_host_read32(controller->bar_base, OHCI_INTERRUPT_STATUS);
    if (revision == 0xffffffffu || control == 0xffffffffu || intr_status == 0xffffffffu) {
        return -1;
    }

    kernel_usb_host_write32(controller->bar_base, OHCI_INTERRUPT_DISABLE, (uint32_t)(OHCI_MIE | OHCI_ALL_INTRS));
    if ((intr_status & OHCI_ALL_INTRS) != 0u) {
        kernel_usb_host_write32(controller->bar_base, OHCI_INTERRUPT_STATUS, intr_status & OHCI_ALL_INTRS);
    }

    return 0;
}

static int kernel_usb_host_probe_execute_ohci_descriptor(const struct kernel_usb_probe_dispatch_context *dispatch,
                                                         struct kernel_usb_probe_execution *execution) {
    uint32_t actual_length;
    uint32_t config_full_length;
    uint16_t max_packet_size0;
    uint16_t config_total_length;
    uint8_t configuration_value;

    if (dispatch == 0 || execution == 0) {
        return -1;
    }
    if (kernel_usb_host_probe_execute_ohci_transfer(dispatch,
                                                    0u,
                                                    USB_DEVICE_DESCRIPTOR_PROBE_LENGTH,
                                                    8u,
                                                    dispatch->snapshot.plan.request_type,
                                                    dispatch->snapshot.plan.request,
                                                    dispatch->snapshot.plan.value,
                                                    dispatch->snapshot.plan.index,
                                                    &g_kernel_usb_ohci_probe_data[0],
                                                    sizeof(g_kernel_usb_ohci_probe_data),
                                                    &actual_length) != 0) {
        return -1;
    }
    kernel_usb_host_probe_copy_descriptor_prefix(execution,
                                                 0,
                                                 &g_kernel_usb_ohci_probe_data[0],
                                                 actual_length);
    if (kernel_usb_host_validate_device_descriptor_prefix(dispatch->snapshot.plan.device.speed,
                                                          execution->descriptor_prefix,
                                                          execution->actual_length,
                                                          &max_packet_size0) != 0) {
        execution->actual_length = 0u;
        memset(execution->descriptor_prefix, 0, sizeof(execution->descriptor_prefix));
        return -1;
    }
    execution->descriptor_valid = 1u;
    execution->max_packet_size0 = max_packet_size0;

    if (kernel_usb_host_probe_execute_ohci_transfer(dispatch,
                                                    0u,
                                                    USB_DEVICE_DESCRIPTOR_FULL_LENGTH,
                                                    max_packet_size0,
                                                    dispatch->snapshot.plan.request_type,
                                                    dispatch->snapshot.plan.request,
                                                    dispatch->snapshot.plan.value,
                                                    dispatch->snapshot.plan.index,
                                                    &g_kernel_usb_ohci_probe_data[0],
                                                    sizeof(g_kernel_usb_ohci_probe_data),
                                                    &actual_length) != 0) {
        return -1;
    }
    kernel_usb_host_probe_copy_descriptor_prefix(execution,
                                                 0,
                                                 &g_kernel_usb_ohci_probe_data[0],
                                                 actual_length);
    if (kernel_usb_host_validate_full_device_descriptor(execution->descriptor_prefix,
                                                        execution->actual_length) != 0) {
        execution->actual_length = 0u;
        execution->descriptor_valid = 0u;
        execution->max_packet_size0 = 0u;
        memset(execution->descriptor_prefix, 0, sizeof(execution->descriptor_prefix));
        return -1;
    }
    execution->descriptor_valid = 1u;
    execution->max_packet_size0 = max_packet_size0;
    if (kernel_usb_host_probe_execute_ohci_set_address(dispatch,
                                                       dispatch->snapshot.plan.device.address) != 0) {
        return -1;
    }
    execution->assigned_address = dispatch->snapshot.plan.device.address;
    if (kernel_usb_host_probe_execute_ohci_transfer(dispatch,
                                                    execution->assigned_address,
                                                    USB_CONFIG_DESCRIPTOR_SHORT_LENGTH,
                                                    max_packet_size0,
                                                    (uint8_t)(USB_REQTYPE_IN |
                                                              USB_REQTYPE_STANDARD |
                                                              USB_REQTYPE_DEVICE),
                                                    USB_REQUEST_GET_DESCRIPTOR,
                                                    (uint16_t)(USB_DESCRIPTOR_TYPE_CONFIG << 8),
                                                    0u,
                                                    &g_kernel_usb_config_probe_data[0],
                                                    sizeof(g_kernel_usb_config_probe_data),
                                                    &actual_length) != 0) {
        return -1;
    }
    kernel_usb_host_probe_copy_config_prefix(execution,
                                             0,
                                             &g_kernel_usb_config_probe_data[0],
                                             actual_length);
    if (kernel_usb_host_validate_config_descriptor_prefix(execution->config_descriptor_prefix,
                                                          actual_length,
                                                          &config_total_length,
                                                          &configuration_value) != 0) {
        return -1;
    }
    execution->config_valid = 1u;
    execution->config_total_length = config_total_length;
    execution->configuration_value = configuration_value;
    config_full_length = config_total_length;
    if (config_full_length > sizeof(g_kernel_usb_config_full_data)) {
        config_full_length = sizeof(g_kernel_usb_config_full_data);
    }
    if (kernel_usb_host_probe_execute_ohci_transfer(dispatch,
                                                    execution->assigned_address,
                                                    (uint16_t)config_full_length,
                                                    max_packet_size0,
                                                    (uint8_t)(USB_REQTYPE_IN |
                                                              USB_REQTYPE_STANDARD |
                                                              USB_REQTYPE_DEVICE),
                                                    USB_REQUEST_GET_DESCRIPTOR,
                                                    (uint16_t)(USB_DESCRIPTOR_TYPE_CONFIG << 8),
                                                    0u,
                                                    &g_kernel_usb_config_full_data[0],
                                                    sizeof(g_kernel_usb_config_full_data),
                                                    &actual_length) != 0) {
        return -1;
    }
    if (kernel_usb_host_validate_full_config_descriptor(&g_kernel_usb_config_full_data[0],
                                                        actual_length,
                                                        config_total_length) != 0) {
        return -1;
    }
    kernel_usb_host_scan_config_descriptor(&g_kernel_usb_config_full_data[0], actual_length, execution);
    execution->config_valid = 1u;
    return 0;
}

static void kernel_usb_host_enable_pci_device(const struct kernel_pci_device_info *info) {
    uint32_t command_status;

    if (info == 0) {
        return;
    }
    command_status = kernel_pci_config_read_u32(info->bus, info->slot, info->function, 0x04u);
    command_status |= (uint32_t)(PCI_COMMAND_IO_SPACE |
                                 PCI_COMMAND_MEMORY_SPACE |
                                 PCI_COMMAND_BUS_MASTER);
    kernel_pci_config_write_u32(info->bus, info->slot, info->function, 0x04u, command_status);
}

static uint8_t kernel_usb_host_choose_bar(const struct kernel_pci_device_info *info, uint8_t *is_mmio_out) {
    if (is_mmio_out == 0) {
        return 0xFFu;
    }

    for (uint8_t i = 0u; i < 6u; ++i) {
        uint32_t bar = info->bars[i];

        if (bar == 0u || bar == 0xffffffffu) {
            continue;
        }
        if (kernel_pci_bar_is_mmio(bar)) {
            *is_mmio_out = 1u;
            return i;
        }
        if ((bar & 0x1u) != 0u && (bar & 0xfffffffcu) != 0u) {
            *is_mmio_out = 0u;
            return i;
        }
    }

    *is_mmio_out = 0u;
    return 0xFFu;
}

static uint32_t kernel_usb_host_bar_base(const struct kernel_pci_device_info *info,
                                         uint8_t bar_index,
                                         uint8_t is_mmio) {
    uint32_t bar;

    if (info == 0 || bar_index >= 6u) {
        return 0u;
    }
    bar = info->bars[bar_index];
    if (is_mmio) {
        return (uint32_t)kernel_pci_bar_base(bar);
    }
    return bar & 0xfffffffcu;
}

static uint32_t kernel_usb_host_bar_size(const struct kernel_pci_device_info *info,
                                         uint8_t bar_index,
                                         uint8_t is_mmio) {
    if (info == 0 || bar_index >= 6u) {
        return 0u;
    }
    if (is_mmio) {
        return (uint32_t)kernel_pci_bar_size(info->bus, info->slot, info->function, bar_index);
    }
    return 0u;
}

static uint32_t kernel_usb_host_detect_port_count(uint8_t kind, uint32_t base, uint8_t is_mmio) {
    if (base == 0u) {
        return 0u;
    }

    switch (kind) {
    case KERNEL_USB_HOST_KIND_UHCI:
        return 2u;
    case KERNEL_USB_HOST_KIND_OHCI:
        if (is_mmio) {
            return kernel_usb_host_read32(base, OHCI_RH_DESCRIPTOR_A) & 0xffu;
        }
        break;
    case KERNEL_USB_HOST_KIND_EHCI:
        if (is_mmio) {
            uint32_t caplen = kernel_usb_host_read32(base, EHCI_CAPLENGTH) & 0xffu;
            uint32_t hcsparams = kernel_usb_host_read32(base, EHCI_HCSPARAMS);

            (void)caplen;
            return hcsparams & 0x0fu;
        }
        break;
    case KERNEL_USB_HOST_KIND_XHCI:
        if (is_mmio) {
            return (kernel_usb_host_read32(base, XHCI_HCSPARAMS1) >> 24) & 0xffu;
        }
        break;
    default:
        break;
    }
    return 0u;
}

static uint32_t kernel_usb_host_port_raw(const struct kernel_usb_host_controller_info *entry,
                                         uint32_t port_index) {
    uint32_t operational;

    if (entry == 0 || entry->bar_base == 0u || port_index >= entry->port_count) {
        return 0u;
    }

    switch (entry->kind) {
    case KERNEL_USB_HOST_KIND_UHCI:
        if (!entry->bar_is_mmio && port_index < 2u) {
            return (uint32_t)kernel_usb_host_read16_io(entry->bar_base, UHCI_PORTSC1 + (port_index * 2u));
        }
        break;
    case KERNEL_USB_HOST_KIND_OHCI:
        if (entry->bar_is_mmio) {
            return kernel_usb_host_read32(entry->bar_base, OHCI_RH_PORT_STATUS_BASE + (port_index * 4u));
        }
        break;
    case KERNEL_USB_HOST_KIND_EHCI:
        if (entry->bar_is_mmio) {
            operational = entry->bar_base + (kernel_usb_host_read32(entry->bar_base, EHCI_CAPLENGTH) & 0xffu);
            return kernel_usb_host_read32(operational, EHCI_PORTSC_BASE + (port_index * 4u));
        }
        break;
    case KERNEL_USB_HOST_KIND_XHCI:
        if (entry->bar_is_mmio) {
            operational = entry->bar_base + (kernel_usb_host_read32(entry->bar_base, XHCI_CAPLENGTH) & 0xffu);
            return kernel_usb_host_read32(operational, XHCI_PORTSC_BASE + (port_index * 16u));
        }
        break;
    default:
        break;
    }

    return 0u;
}

static void kernel_usb_host_decode_port_status(uint32_t raw, struct kernel_usb_host_port_status *status) {
    if (status == 0) {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->raw = raw;
    status->connected = (raw & USB_PORT_CONNECTED) != 0u ? 1u : 0u;
    status->enabled = (raw & USB_PORT_ENABLED) != 0u ? 1u : 0u;
    status->powered = (raw & USB_PORT_POWERED) != 0u ? 1u : 0u;
    status->owner = (raw & USB_PORT_OWNER) != 0u ? 1u : 0u;
    status->reset = (raw & USB_PORT_RESET_ACTIVE) != 0u ? 1u : 0u;
    status->speed = KERNEL_USB_HOST_PORT_SPEED_UNKNOWN;
}

static void kernel_usb_host_decode_port_status_kind(uint8_t kind,
                                                    uint32_t raw,
                                                    struct kernel_usb_host_port_status *status) {
    kernel_usb_host_decode_port_status(raw, status);
    if (status == 0) {
        return;
    }

    switch (kind) {
    case KERNEL_USB_HOST_KIND_UHCI:
        status->powered = 1u;
        status->reset = (raw & USB_PORT_RESET_ACTIVE) != 0u ? 1u : 0u;
        status->speed = (raw & UHCI_PORT_LOW_SPEED) != 0u ?
                            KERNEL_USB_HOST_PORT_SPEED_LOW :
                            KERNEL_USB_HOST_PORT_SPEED_FULL;
        break;
    case KERNEL_USB_HOST_KIND_OHCI:
        status->powered = (raw & OHCI_PORT_POWER_STATUS) != 0u ? 1u : 0u;
        status->reset = (raw & OHCI_PORT_RESET_STATUS) != 0u ? 1u : 0u;
        status->speed = (raw & OHCI_PORT_LOW_SPEED) != 0u ?
                            KERNEL_USB_HOST_PORT_SPEED_LOW :
                            KERNEL_USB_HOST_PORT_SPEED_FULL;
        break;
    case KERNEL_USB_HOST_KIND_EHCI: {
        uint32_t line_status = (raw >> EHCI_PORT_LINE_STATUS_SHIFT) & EHCI_PORT_LINE_STATUS_MASK;

        status->reset = (raw & EHCI_PORT_RESET) != 0u ? 1u : 0u;
        status->speed = status->owner ? KERNEL_USB_HOST_PORT_SPEED_FULL :
                        (line_status == 1u ? KERNEL_USB_HOST_PORT_SPEED_LOW :
                         KERNEL_USB_HOST_PORT_SPEED_HIGH);
        break;
    }
    case KERNEL_USB_HOST_KIND_XHCI: {
        uint32_t speed_id = (raw >> XHCI_PORT_SPEED_SHIFT) & XHCI_PORT_SPEED_MASK;

        status->powered = (raw & XHCI_PORT_POWER) != 0u ? 1u : 0u;
        status->reset = (raw & XHCI_PORT_RESET) != 0u ? 1u : 0u;
        if (speed_id >= 4u) {
            status->speed = KERNEL_USB_HOST_PORT_SPEED_SUPER;
        } else if (speed_id == 3u) {
            status->speed = KERNEL_USB_HOST_PORT_SPEED_HIGH;
        } else if (speed_id == 2u) {
            status->speed = KERNEL_USB_HOST_PORT_SPEED_LOW;
        } else if (speed_id == 1u) {
            status->speed = KERNEL_USB_HOST_PORT_SPEED_FULL;
        }
        break;
    }
    default:
        break;
    }

    if (!status->connected) {
        status->speed = KERNEL_USB_HOST_PORT_SPEED_UNKNOWN;
    }
}

static void kernel_usb_host_store_controller(struct kernel_usb_host_state *state,
                                             const struct kernel_pci_device_info *info) {
    struct kernel_usb_host_controller_info *entry;
    struct kernel_usb_host_port_status port_status;
    uint8_t kind;
    uint8_t bar_is_mmio;
    uint8_t bar_index;
    uint32_t bar_base;
    uint32_t bar_size;
    uint32_t port_count;

    if (state->controllers >= KERNEL_USB_HOST_MAX_CONTROLLERS) {
        return;
    }

    kind = kernel_usb_host_kind_from_prog_if(info->prog_if);
    bar_index = kernel_usb_host_choose_bar(info, &bar_is_mmio);
    bar_base = kernel_usb_host_bar_base(info, bar_index, bar_is_mmio);
    bar_size = kernel_usb_host_bar_size(info, bar_index, bar_is_mmio);
    port_count = kernel_usb_host_detect_port_count(kind, bar_base, bar_is_mmio);

    entry = &state->entries[state->controllers];
    memset(entry, 0, sizeof(*entry));
    entry->kind = kind;
    entry->bus = info->bus;
    entry->slot = info->slot;
    entry->function = info->function;
    entry->irq_line = info->irq_line;
    entry->prog_if = info->prog_if;
    entry->bar_index = bar_index;
    entry->bar_is_mmio = bar_is_mmio;
    entry->vendor_id = info->vendor_id;
    entry->device_id = info->device_id;
    entry->bar_base = bar_base;
    entry->bar_size = bar_size;
    entry->port_count = port_count;

    switch (kind) {
    case KERNEL_USB_HOST_KIND_UHCI:
        state->uhci++;
        break;
    case KERNEL_USB_HOST_KIND_OHCI:
        state->ohci++;
        break;
    case KERNEL_USB_HOST_KIND_EHCI:
        state->ehci++;
        break;
    case KERNEL_USB_HOST_KIND_XHCI:
        state->xhci++;
        break;
    default:
        break;
    }

    for (uint32_t port = 0u; port < entry->port_count; ++port) {
        kernel_usb_host_decode_port_status_kind(entry->kind,
                                                kernel_usb_host_port_raw(entry, port),
                                                &port_status);
        if (port_status.connected) {
            state->connected_ports++;
            if (port_status.speed == KERNEL_USB_HOST_PORT_SPEED_HIGH) {
                state->connected_high_speed_ports++;
            }
            if (port_status.speed == KERNEL_USB_HOST_PORT_SPEED_SUPER) {
                state->connected_super_speed_ports++;
            }
            kernel_usb_host_register_root_device(state,
                                                 entry,
                                                 (uint8_t)state->controllers,
                                                 port,
                                                 &port_status);
        }
        kernel_debug_printf("usb-host: port controller=%s index=%u connected=%u enabled=%u powered=%u owner=%u reset=%u speed=%s raw=%x\n",
                            kernel_usb_host_kind_name(kind),
                            (unsigned int)port,
                            (unsigned int)port_status.connected,
                            (unsigned int)port_status.enabled,
                            (unsigned int)port_status.powered,
                            (unsigned int)port_status.owner,
                            (unsigned int)port_status.reset,
                            kernel_usb_host_speed_name(port_status.speed),
                            (unsigned int)port_status.raw);
    }

    kernel_debug_printf("usb-host: controller=%s pci=%x:%x loc=%x:%x.%x irq=%x bar=%u %s=%x size=%x ports=%u prog_if=%x\n",
                        kernel_usb_host_kind_name(kind),
                        (unsigned int)info->vendor_id,
                        (unsigned int)info->device_id,
                        (unsigned int)info->bus,
                        (unsigned int)info->slot,
                        (unsigned int)info->function,
                        (unsigned int)info->irq_line,
                        (unsigned int)bar_index,
                        bar_is_mmio ? "mmio" : "io",
                        (unsigned int)bar_base,
                        (unsigned int)bar_size,
                        (unsigned int)port_count,
                        (unsigned int)info->prog_if);
    state->controllers++;
}

static int kernel_usb_host_probe_cb(const struct kernel_pci_device_info *info, void *ctx_ptr) {
    struct kernel_usb_host_state *state = (struct kernel_usb_host_state *)ctx_ptr;

    if (info == 0 || state == 0) {
        return 0;
    }
    if (info->class_code != PCI_CLASS_SERIAL_BUS || info->subclass != PCI_SUBCLASS_USB) {
        return 0;
    }

    kernel_usb_host_enable_pci_device(info);
    kernel_usb_host_store_controller(state, info);
    return 0;
}

static void kernel_usb_host_execute_all_pending(void) {
    struct kernel_usb_probe_execution execution;

    while (kernel_usb_probe_execute_next(0u, &execution) == 0) {
        kernel_debug_printf("usb-host: probe-exec index=%u transport=%s result=%u status=%d\n",
                            (unsigned int)execution.dispatch.snapshot_index,
                            kernel_usb_host_kind_name(execution.dispatch.transport_kind),
                            (unsigned int)execution.result,
                            execution.status_code);
    }
}

void kernel_usb_host_init(void) {
    memset(&g_kernel_usb_host_state, 0, sizeof(g_kernel_usb_host_state));
    (void)kernel_pci_enumerate(kernel_usb_host_probe_cb, &g_kernel_usb_host_state);
    kernel_usb_host_build_probe_snapshots(&g_kernel_usb_host_state);
    kernel_usb_host_execute_all_pending();
    kernel_debug_printf("usb-host: summary controllers=%u uhci=%u ohci=%u ehci=%u xhci=%u\n",
                        (unsigned int)g_kernel_usb_host_state.controllers,
                        (unsigned int)g_kernel_usb_host_state.uhci,
                        (unsigned int)g_kernel_usb_host_state.ohci,
                        (unsigned int)g_kernel_usb_host_state.ehci,
                        (unsigned int)g_kernel_usb_host_state.xhci);
    kernel_debug_printf("usb-host: connected-ports=%u\n",
                        (unsigned int)g_kernel_usb_host_state.connected_ports);
    kernel_debug_printf("usb-host: connected-high=%u connected-super=%u\n",
                        (unsigned int)g_kernel_usb_host_state.connected_high_speed_ports,
                        (unsigned int)g_kernel_usb_host_state.connected_super_speed_ports);
    kernel_debug_printf("usb-host: root-devices=%u audio-candidates=%u\n",
                        (unsigned int)g_kernel_usb_host_state.root_devices,
                        (unsigned int)g_kernel_usb_host_state.audio_candidates);
    kernel_debug_printf("usb-host: devices=%u ready-for-enum=%u\n",
                        (unsigned int)g_kernel_usb_host_state.devices,
                        (unsigned int)g_kernel_usb_host_state.devices_ready_for_enum);
    kernel_debug_printf("usb-host: control-ready=%u needs-companion=%u\n",
                        (unsigned int)g_kernel_usb_host_state.devices_control_ready,
                        (unsigned int)g_kernel_usb_host_state.devices_needing_companion);
    kernel_debug_printf("usb-host: companion-present=%u\n",
                        (unsigned int)g_kernel_usb_host_state.devices_with_companion_present);
    kernel_debug_printf("usb-host: handoff-ready=%u\n",
                        (unsigned int)g_kernel_usb_host_state.devices_handoff_ready);
    kernel_debug_printf("usb-host: control-path-ready=%u\n",
                        (unsigned int)g_kernel_usb_host_state.devices_control_path_ready);
    kernel_debug_printf("usb-host: probe-targets=%u audio-probe-targets=%u\n",
                        (unsigned int)g_kernel_usb_host_state.device_probe_targets,
                        (unsigned int)g_kernel_usb_host_state.audio_probe_targets);
    kernel_debug_printf("usb-host: probe-snapshots=%u audio-probe-snapshots=%u\n",
                        (unsigned int)g_kernel_usb_host_state.probe_snapshots,
                        (unsigned int)g_kernel_usb_host_state.audio_probe_snapshots);
    kernel_debug_printf("usb-host: probe-dispatch-ready=%u audio-probe-dispatch-ready=%u\n",
                        (unsigned int)g_kernel_usb_host_state.probe_dispatch_ready,
                        (unsigned int)g_kernel_usb_host_state.audio_probe_dispatch_ready);
    kernel_debug_printf("usb-host: probe-exec-ready=%u audio-probe-exec-ready=%u\n",
                        (unsigned int)g_kernel_usb_host_state.probe_exec_ready,
                        (unsigned int)g_kernel_usb_host_state.audio_probe_exec_ready);
    kernel_debug_printf("usb-host: probe-descriptor-ready=%u audio-probe-descriptor-ready=%u\n",
                        (unsigned int)g_kernel_usb_host_state.probe_descriptor_ready,
                        (unsigned int)g_kernel_usb_host_state.audio_probe_descriptor_ready);
    for (uint32_t i = 0u; i < g_kernel_usb_host_state.devices; ++i) {
        const struct kernel_usb_device_info *device = &g_kernel_usb_host_state.device_entries[i];

        kernel_debug_printf("usb-host: device addr=%u controller=%u port=%u kind=%s effective=%u/%s companion=%u/%s speed=%s state=%u flags=%x\n",
                            (unsigned int)device->address,
                            (unsigned int)device->controller_index,
                            (unsigned int)device->port_index,
                            kernel_usb_host_kind_name(device->controller_kind),
                            device->effective_controller_index == 0xffu ?
                                0xffffffffu :
                                (unsigned int)device->effective_controller_index,
                            kernel_usb_host_kind_name(device->effective_controller_kind),
                            device->companion_controller_index == 0xffu ?
                                0xffffffffu :
                                (unsigned int)device->companion_controller_index,
                            kernel_usb_host_kind_name(device->companion_controller_kind),
                            kernel_usb_host_speed_name(device->speed),
                            (unsigned int)device->state,
                            (unsigned int)device->flags);
    }
    for (uint32_t i = 0u; i < g_kernel_usb_host_state.probe_snapshots; ++i) {
        const struct kernel_usb_probe_snapshot *snapshot = &g_kernel_usb_host_state.probe_snapshot_entries[i];

        kernel_debug_printf("usb-host: probe index=%u device=%u audio=%u req=%x type=%x value=%x indexv=%x len=%u status=%u\n",
                            (unsigned int)i,
                            (unsigned int)snapshot->device_index,
                            (unsigned int)snapshot->audio_candidate,
                            (unsigned int)snapshot->plan.request,
                            (unsigned int)snapshot->plan.request_type,
                            (unsigned int)snapshot->plan.value,
                            (unsigned int)snapshot->plan.index,
                            (unsigned int)snapshot->plan.length,
                            (unsigned int)snapshot->status);
        if (snapshot->descriptor_valid) {
            kernel_debug_printf("usb-host: probe descriptor device=%u addr=%u mps0=%u prefix=%x:%x:%x:%x:%x:%x:%x:%x\n",
                                (unsigned int)snapshot->device_index,
                                (unsigned int)snapshot->assigned_address,
                                (unsigned int)snapshot->max_packet_size0,
                                (unsigned int)snapshot->descriptor_prefix[0],
                                (unsigned int)snapshot->descriptor_prefix[1],
                                (unsigned int)snapshot->descriptor_prefix[2],
                                (unsigned int)snapshot->descriptor_prefix[3],
                                (unsigned int)snapshot->descriptor_prefix[4],
                                (unsigned int)snapshot->descriptor_prefix[5],
                                (unsigned int)snapshot->descriptor_prefix[6],
                                (unsigned int)snapshot->descriptor_prefix[7]);
        }
        if (snapshot->config_valid) {
            kernel_debug_printf("usb-host: probe config device=%u cfg=%u total=%u ifaces=%u eps=%u audio=%u prefix=%x:%x:%x:%x:%x:%x:%x:%x:%x\n",
                                (unsigned int)snapshot->device_index,
                                (unsigned int)snapshot->configuration_value,
                                (unsigned int)snapshot->config_total_length,
                                (unsigned int)snapshot->interface_count,
                                (unsigned int)snapshot->endpoint_count,
                                (unsigned int)snapshot->audio_class_detected,
                                (unsigned int)snapshot->config_descriptor_prefix[0],
                                (unsigned int)snapshot->config_descriptor_prefix[1],
                                (unsigned int)snapshot->config_descriptor_prefix[2],
                                (unsigned int)snapshot->config_descriptor_prefix[3],
                                (unsigned int)snapshot->config_descriptor_prefix[4],
                                (unsigned int)snapshot->config_descriptor_prefix[5],
                                (unsigned int)snapshot->config_descriptor_prefix[6],
                                (unsigned int)snapshot->config_descriptor_prefix[7],
                                (unsigned int)snapshot->config_descriptor_prefix[8]);
        }
    }
}

uint32_t kernel_usb_host_controller_count(void) {
    return g_kernel_usb_host_state.controllers;
}

uint32_t kernel_usb_host_uhci_count(void) {
    return g_kernel_usb_host_state.uhci;
}

uint32_t kernel_usb_host_ohci_count(void) {
    return g_kernel_usb_host_state.ohci;
}

uint32_t kernel_usb_host_ehci_count(void) {
    return g_kernel_usb_host_state.ehci;
}

uint32_t kernel_usb_host_xhci_count(void) {
    return g_kernel_usb_host_state.xhci;
}

uint32_t kernel_usb_host_connected_port_count(void) {
    return g_kernel_usb_host_state.connected_ports;
}

uint32_t kernel_usb_host_connected_high_speed_port_count(void) {
    return g_kernel_usb_host_state.connected_high_speed_ports;
}

uint32_t kernel_usb_host_connected_super_speed_port_count(void) {
    return g_kernel_usb_host_state.connected_super_speed_ports;
}

uint32_t kernel_usb_host_root_device_count(void) {
    return g_kernel_usb_host_state.root_devices;
}

uint32_t kernel_usb_host_audio_candidate_count(void) {
    return g_kernel_usb_host_state.audio_candidates;
}

uint32_t kernel_usb_device_count(void) {
    return g_kernel_usb_host_state.devices;
}

uint32_t kernel_usb_device_ready_for_enum_count(void) {
    return g_kernel_usb_host_state.devices_ready_for_enum;
}

uint32_t kernel_usb_device_control_ready_count(void) {
    return g_kernel_usb_host_state.devices_control_ready;
}

uint32_t kernel_usb_device_needs_companion_count(void) {
    return g_kernel_usb_host_state.devices_needing_companion;
}

uint32_t kernel_usb_device_companion_present_count(void) {
    return g_kernel_usb_host_state.devices_with_companion_present;
}

uint32_t kernel_usb_device_handoff_ready_count(void) {
    return g_kernel_usb_host_state.devices_handoff_ready;
}

uint32_t kernel_usb_device_control_path_ready_count(void) {
    return g_kernel_usb_host_state.devices_control_path_ready;
}

uint32_t kernel_usb_device_probe_target_count(void) {
    return g_kernel_usb_host_state.device_probe_targets;
}

uint32_t kernel_usb_audio_probe_target_count(void) {
    return g_kernel_usb_host_state.audio_probe_targets;
}

uint32_t kernel_usb_probe_snapshot_count(void) {
    return g_kernel_usb_host_state.probe_snapshots;
}

uint32_t kernel_usb_audio_probe_snapshot_count(void) {
    return g_kernel_usb_host_state.audio_probe_snapshots;
}

uint32_t kernel_usb_probe_dispatch_ready_count(void) {
    return g_kernel_usb_host_state.probe_dispatch_ready;
}

uint32_t kernel_usb_audio_probe_dispatch_ready_count(void) {
    return g_kernel_usb_host_state.audio_probe_dispatch_ready;
}

uint32_t kernel_usb_probe_exec_ready_count(void) {
    return g_kernel_usb_host_state.probe_exec_ready;
}

uint32_t kernel_usb_audio_probe_exec_ready_count(void) {
    return g_kernel_usb_host_state.audio_probe_exec_ready;
}

uint32_t kernel_usb_probe_descriptor_ready_count(void) {
    return g_kernel_usb_host_state.probe_descriptor_ready;
}

uint32_t kernel_usb_audio_probe_descriptor_ready_count(void) {
    return g_kernel_usb_host_state.audio_probe_descriptor_ready;
}

uint32_t kernel_usb_audio_class_probe_count(void) {
    uint32_t count = 0u;

    for (uint32_t i = 0u; i < g_kernel_usb_host_state.probe_snapshots; ++i) {
        const struct kernel_usb_probe_snapshot *snapshot = &g_kernel_usb_host_state.probe_snapshot_entries[i];

        if (snapshot->audio_class_detected) {
            count++;
        }
    }

    return count;
}

int kernel_usb_probe_dispatch_next(uint8_t audio_only,
                                   struct kernel_usb_probe_snapshot *info_out,
                                   uint32_t *match_index_out) {
    uint32_t start_index;

    if (info_out == 0) {
        return -1;
    }

    start_index = audio_only ? g_kernel_usb_host_state.audio_probe_dispatch_cursor :
                               g_kernel_usb_host_state.probe_dispatch_cursor;

    for (uint32_t pass = 0u; pass < 2u; ++pass) {
        uint32_t begin = pass == 0u ? start_index : 0u;
        uint32_t end = pass == 0u ? g_kernel_usb_host_state.probe_snapshots : start_index;

        for (uint32_t i = begin; i < end; ++i) {
            struct kernel_usb_probe_snapshot *snapshot = &g_kernel_usb_host_state.probe_snapshot_entries[i];

            if (snapshot->status != KERNEL_USB_PROBE_STATUS_DISPATCH_READY) {
                continue;
            }
            if (audio_only && !snapshot->audio_candidate) {
                continue;
            }

            *info_out = *snapshot;
            if (match_index_out != 0) {
                *match_index_out = i;
            }
            kernel_usb_host_probe_snapshot_set_status(&g_kernel_usb_host_state,
                                                      snapshot,
                                                      KERNEL_USB_PROBE_STATUS_DISPATCH_SELECTED);
            if (audio_only) {
                g_kernel_usb_host_state.audio_probe_dispatch_cursor = i + 1u;
            }
            g_kernel_usb_host_state.probe_dispatch_cursor = i + 1u;
            return 0;
        }
    }

    return -1;
}

int kernel_usb_probe_dispatch_context_next(uint8_t audio_only,
                                           struct kernel_usb_probe_dispatch_context *info_out) {
    struct kernel_usb_probe_snapshot snapshot;
    uint32_t snapshot_index;
    uint8_t effective_controller_index;

    if (info_out == 0) {
        return -1;
    }
    if (kernel_usb_probe_dispatch_next(audio_only, &snapshot, &snapshot_index) != 0) {
        return -1;
    }

    memset(info_out, 0, sizeof(*info_out));
    info_out->snapshot_index = snapshot_index;
    info_out->snapshot = snapshot;

    effective_controller_index = snapshot.plan.device.effective_controller_index;
    if (kernel_usb_host_controller_info(effective_controller_index, &info_out->effective_controller) != 0) {
        return -1;
    }
    if (kernel_usb_host_port_status(effective_controller_index,
                                    snapshot.plan.device.port_index,
                                    &info_out->effective_port_status) != 0) {
        return -1;
    }
    info_out->transport_kind = kernel_usb_host_transport_kind(&info_out->effective_controller);
    info_out->transport_available = kernel_usb_host_transport_available(info_out->transport_kind);

    return 0;
}

int kernel_usb_probe_execute_next(uint8_t audio_only,
                                  struct kernel_usb_probe_execution *info_out) {
    struct kernel_usb_probe_dispatch_context dispatch;
    struct kernel_usb_probe_snapshot *snapshot;
    int execute_status;
    uint8_t snapshot_status;

    if (info_out == 0) {
        return -1;
    }
    if (kernel_usb_probe_dispatch_context_next(audio_only, &dispatch) != 0) {
        return -1;
    }

    memset(info_out, 0, sizeof(*info_out));
    info_out->dispatch = dispatch;
    info_out->result = kernel_usb_probe_result_for_transport(dispatch.transport_kind);
    info_out->status_code = -1;
    snapshot_status = KERNEL_USB_PROBE_STATUS_EXEC_NO_TRANSPORT;

    if (!dispatch.transport_available) {
        execute_status = -1;
    } else if (dispatch.transport_kind == KERNEL_USB_HOST_KIND_UHCI) {
        execute_status = kernel_usb_host_probe_execute_uhci_descriptor(&dispatch, info_out);
        if (execute_status == 0) {
            info_out->result = KERNEL_USB_PROBE_EXEC_RESULT_UHCI_CONFIG_DESCRIPTOR_OK;
            info_out->status_code = 0;
            snapshot_status = KERNEL_USB_PROBE_STATUS_CONFIG_READY;
        } else if (execute_status > 0) {
            info_out->result = KERNEL_USB_PROBE_EXEC_RESULT_UHCI_DESCRIPTOR_TIMEOUT;
            info_out->status_code = -1;
            snapshot_status = KERNEL_USB_PROBE_STATUS_EXEC_READY;
        } else {
            info_out->result = KERNEL_USB_PROBE_EXEC_RESULT_UHCI_DESCRIPTOR_ERROR;
            info_out->status_code = execute_status;
            snapshot_status = KERNEL_USB_PROBE_STATUS_EXEC_NO_TRANSPORT;
        }
    } else if (dispatch.transport_kind == KERNEL_USB_HOST_KIND_OHCI) {
        execute_status = kernel_usb_host_probe_execute_ohci_descriptor(&dispatch, info_out);
        if (execute_status == 0) {
            info_out->result = KERNEL_USB_PROBE_EXEC_RESULT_OHCI_CONFIG_DESCRIPTOR_OK;
            info_out->status_code = 0;
            snapshot_status = KERNEL_USB_PROBE_STATUS_CONFIG_READY;
        } else if (execute_status > 0) {
            info_out->result = KERNEL_USB_PROBE_EXEC_RESULT_OHCI_DESCRIPTOR_TIMEOUT;
            info_out->status_code = -1;
            snapshot_status = KERNEL_USB_PROBE_STATUS_EXEC_READY;
        } else {
            info_out->result = KERNEL_USB_PROBE_EXEC_RESULT_OHCI_DESCRIPTOR_ERROR;
            info_out->status_code = execute_status;
            snapshot_status = KERNEL_USB_PROBE_STATUS_EXEC_NO_TRANSPORT;
        }
    }

    if (dispatch.snapshot_index < g_kernel_usb_host_state.probe_snapshots) {
        snapshot = &g_kernel_usb_host_state.probe_snapshot_entries[dispatch.snapshot_index];
        if (info_out->actual_length != 0u) {
            kernel_usb_host_probe_copy_descriptor_prefix(info_out,
                                                         snapshot,
                                                         info_out->descriptor_prefix,
                                                         info_out->actual_length);
            snapshot->descriptor_valid = info_out->descriptor_valid;
            snapshot->assigned_address = info_out->assigned_address;
            snapshot->max_packet_size0 = info_out->max_packet_size0;
            kernel_usb_host_probe_copy_config_prefix(info_out,
                                                     snapshot,
                                                     info_out->config_descriptor_prefix,
                                                     USB_CONFIG_DESCRIPTOR_SHORT_LENGTH);
            snapshot->config_valid = info_out->config_valid;
            snapshot->audio_class_detected = info_out->audio_class_detected;
            snapshot->config_total_length = info_out->config_total_length;
            snapshot->configuration_value = info_out->configuration_value;
            snapshot->interface_count = info_out->interface_count;
            snapshot->endpoint_count = info_out->endpoint_count;
        } else {
            memset(snapshot->descriptor_prefix, 0, sizeof(snapshot->descriptor_prefix));
            snapshot->actual_length = 0u;
            snapshot->descriptor_valid = 0u;
            snapshot->assigned_address = 0u;
            snapshot->max_packet_size0 = 0u;
            memset(snapshot->config_descriptor_prefix, 0, sizeof(snapshot->config_descriptor_prefix));
            snapshot->config_valid = 0u;
            snapshot->audio_class_detected = 0u;
            snapshot->config_total_length = 0u;
            snapshot->configuration_value = 0u;
            snapshot->interface_count = 0u;
            snapshot->endpoint_count = 0u;
        }
        kernel_usb_host_probe_snapshot_set_status(&g_kernel_usb_host_state,
                                                  snapshot,
                                                  snapshot_status);
    }

    return 0;
}

int kernel_usb_host_controller_info(uint32_t index, struct kernel_usb_host_controller_info *info_out) {
    if (info_out == 0 || index >= g_kernel_usb_host_state.controllers) {
        return -1;
    }
    *info_out = g_kernel_usb_host_state.entries[index];
    return 0;
}

int kernel_usb_host_port_status(uint32_t controller_index,
                                uint32_t port_index,
                                struct kernel_usb_host_port_status *status_out) {
    if (status_out == 0 || controller_index >= g_kernel_usb_host_state.controllers) {
        return -1;
    }
    kernel_usb_host_decode_port_status_kind(
        g_kernel_usb_host_state.entries[controller_index].kind,
        kernel_usb_host_port_raw(&g_kernel_usb_host_state.entries[controller_index], port_index),
        status_out);
    return 0;
}

int kernel_usb_host_root_device_info(uint32_t index, struct kernel_usb_host_root_device_info *info_out) {
    if (info_out == 0 || index >= g_kernel_usb_host_state.root_devices) {
        return -1;
    }
    *info_out = g_kernel_usb_host_state.root_device_entries[index];
    return 0;
}

int kernel_usb_device_info(uint32_t index, struct kernel_usb_device_info *info_out) {
    if (info_out == 0 || index >= g_kernel_usb_host_state.devices) {
        return -1;
    }
    *info_out = g_kernel_usb_host_state.device_entries[index];
    return 0;
}

int kernel_usb_device_probe_target(uint32_t start_index,
                                   uint8_t audio_only,
                                   struct kernel_usb_device_info *info_out,
                                   uint32_t *match_index_out) {
    uint32_t i;

    if (info_out == 0) {
        return -1;
    }

    for (i = start_index; i < g_kernel_usb_host_state.devices; ++i) {
        const struct kernel_usb_device_info *device = &g_kernel_usb_host_state.device_entries[i];

        if ((device->flags & KERNEL_USB_DEVICE_FLAG_CONTROL_PATH_READY) == 0u) {
            continue;
        }
        if (audio_only && (device->flags & KERNEL_USB_DEVICE_FLAG_AUDIO_CANDIDATE) == 0u) {
            continue;
        }
        *info_out = *device;
        if (match_index_out != 0) {
            *match_index_out = i;
        }
        return 0;
    }

    return -1;
}

int kernel_usb_device_probe_plan(uint32_t start_index,
                                 uint8_t audio_only,
                                 struct kernel_usb_probe_plan *plan_out,
                                 uint32_t *match_index_out) {
    struct kernel_usb_device_info device;

    if (plan_out == 0) {
        return -1;
    }
    if (kernel_usb_device_probe_target(start_index, audio_only, &device, match_index_out) != 0) {
        return -1;
    }

    memset(plan_out, 0, sizeof(*plan_out));
    plan_out->device = device;
    plan_out->request_type = (uint8_t)(USB_REQTYPE_IN |
                                       USB_REQTYPE_STANDARD |
                                       USB_REQTYPE_DEVICE);
    plan_out->request = USB_REQUEST_GET_DESCRIPTOR;
    plan_out->value = (uint16_t)(USB_DESCRIPTOR_TYPE_DEVICE << 8);
    plan_out->index = 0u;
    plan_out->length = USB_DEVICE_DESCRIPTOR_PROBE_LENGTH;
    return 0;
}

int kernel_usb_probe_snapshot_info(uint32_t index, struct kernel_usb_probe_snapshot *info_out) {
    if (info_out == 0 || index >= g_kernel_usb_host_state.probe_snapshots) {
        return -1;
    }
    *info_out = g_kernel_usb_host_state.probe_snapshot_entries[index];
    return 0;
}
