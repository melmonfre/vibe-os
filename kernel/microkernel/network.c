#include <kernel/kernel_string.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/event.h>
#include <kernel/hal/io.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/network.h>
#include <kernel/microkernel/service.h>
#include <kernel/scheduler.h>
#include <kernel/userland_service.h>
#include <stddef.h>
#include <string.h>

#define VIRTIO_PCI_VENDOR_ID 0x1AF4u
#define VIRTIO_PCI_DEVICE_NET_LEGACY 0x1000u
#define VIRTIO_PCI_DEVICE_NET_MODERN 0x1041u

#define VIRTIO_CONFIG_DEVICE_FEATURES 0u
#define VIRTIO_CONFIG_GUEST_FEATURES 4u
#define VIRTIO_CONFIG_QUEUE_ADDRESS 8u
#define VIRTIO_CONFIG_QUEUE_SIZE 12u
#define VIRTIO_CONFIG_QUEUE_SELECT 14u
#define VIRTIO_CONFIG_QUEUE_NOTIFY 16u
#define VIRTIO_CONFIG_ISR_STATUS 19u
#define VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI 20u
#define VIRTIO_CONFIG_DEVICE_STATUS 18u

#define VIRTIO_CONFIG_DEVICE_STATUS_RESET 0u
#define VIRTIO_CONFIG_DEVICE_STATUS_ACK 1u
#define VIRTIO_CONFIG_DEVICE_STATUS_DRIVER 2u
#define VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK 4u
#define VIRTIO_CONFIG_DEVICE_STATUS_FAILED 128u

#define VIRTIO_NET_CONFIG_MAC 0u
#define VIRTIO_NET_CONFIG_STATUS 6u
#define VIRTIO_NET_F_MAC (1u << 5)
#define VIRTIO_NET_F_STATUS (1u << 16)
#define VIRTIO_NET_S_LINK_UP 0x1u
#define VIRTIO_PAGE_SIZE 4096u
#define MK_NETWORK_VIRTIO_RING_ALIGN 4096u
#define MK_NETWORK_VIRTIO_MAX_QUEUE_SIZE 256u
#define MK_NETWORK_VIRTIO_QUEUE_COUNT 2u
#define MK_NETWORK_VIRTIO_RX_QUEUE_INDEX 0u
#define MK_NETWORK_VIRTIO_TX_QUEUE_INDEX 1u
#define MK_NETWORK_VIRTIO_DESC_F_NEXT 1u
#define MK_NETWORK_VIRTIO_DESC_F_WRITE 2u
#define MK_NETWORK_VIRTIO_HDR_SIZE 10u
#define MK_NETWORK_VIRTIO_RX_SLOT_COUNT 8u
#define MK_NETWORK_VIRTIO_RX_BUFFER_BYTES 2048u
#define MK_NETWORK_VIRTIO_TX_FRAME_BYTES 64u
#define MK_NETWORK_EVENT_SUBSCRIBERS 8u
#define MK_NETWORK_EVENT_QUEUE_SIZE 16u

struct mk_network_scan_entry_state {
    char ssid[MK_NETWORK_SSID_MAX + 1];
    uint32_t signal_strength;
    uint32_t security;
};

#define MK_NETWORK_SOCKET_MAX 8
#define MK_NETWORK_SOCKET_RX_CAPACITY 512
#define MK_NETWORK_SOCKET_ADDRESS_MAX ((uint32_t)sizeof(struct sockaddr_storage))

struct mk_network_socket_state {
    int used;
    uint32_t owner_pid;
    uint32_t domain;
    uint32_t type;
    uint32_t protocol;
    int bound;
    int listening;
    int connected;
    int peer_handle;
    int pending_accept_handle;
    uint32_t address_length;
    struct sockaddr_storage address;
    uint32_t rx_size;
    uint8_t rx_buffer[MK_NETWORK_SOCKET_RX_CAPACITY];
};

struct mk_network_event_subscription {
    int pid;
    process_t *process;
    kernel_mailbox_t mailbox;
    struct mk_network_event events[MK_NETWORK_EVENT_QUEUE_SIZE];
};

struct mk_network_pci_probe_state {
    int present;
    int virtio_legacy_ready;
    int virtio_queue_ready;
    int virtio_link_up;
    int virtio_status_valid;
    int mac_valid;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t io_base;
    uint8_t mac[6];
    uint16_t queue_size[MK_NETWORK_VIRTIO_QUEUE_COUNT];
    char if_name[MK_NETWORK_IF_NAME_MAX];
    char backend_name[24];
};

struct mk_network_vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct mk_network_vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[MK_NETWORK_VIRTIO_MAX_QUEUE_SIZE];
    uint16_t used_event;
} __attribute__((packed));

struct mk_network_vring_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct mk_network_vring_used {
    uint16_t flags;
    uint16_t idx;
    struct mk_network_vring_used_elem ring[MK_NETWORK_VIRTIO_MAX_QUEUE_SIZE];
    uint16_t avail_event;
} __attribute__((packed));

struct mk_network_virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
} __attribute__((packed));

struct mk_network_virtio_legacy_queue {
    struct mk_network_vring_desc *desc;
    struct mk_network_vring_avail *avail;
    struct mk_network_vring_used *used;
    uint32_t queue_size;
    uint32_t used_offset;
    uint32_t total_bytes;
    uintptr_t phys_addr;
    uint16_t next_avail_idx;
    uint16_t last_used_idx;
};

struct mk_network_virtio_link_state {
    int rx_posted;
    int tx_busy;
    int tx_smoke_sent;
    int tx_smoke_done;
    int rx_activity_seen;
    int rx_arp_reply_seen;
    int tx_smoke_logged;
    int rx_activity_logged;
    int rx_arp_reply_logged;
    uint32_t tx_frames_submitted;
    uint32_t tx_frames_completed;
    uint32_t rx_frames_completed;
    uint32_t last_tx_frame_bytes;
};

static uint8_t g_network_virtio_queue_storage[MK_NETWORK_VIRTIO_QUEUE_COUNT][16384]
    __attribute__((aligned(MK_NETWORK_VIRTIO_RING_ALIGN)));
static struct mk_network_virtio_legacy_queue
    g_network_virtio_queues[MK_NETWORK_VIRTIO_QUEUE_COUNT];
static struct mk_network_virtio_net_hdr
    g_network_virtio_rx_hdr[MK_NETWORK_VIRTIO_RX_SLOT_COUNT] __attribute__((aligned(16)));
static uint8_t g_network_virtio_rx_payload[MK_NETWORK_VIRTIO_RX_SLOT_COUNT][MK_NETWORK_VIRTIO_RX_BUFFER_BYTES]
    __attribute__((aligned(16)));
static struct mk_network_virtio_net_hdr g_network_virtio_tx_hdr __attribute__((aligned(16)));
static uint8_t g_network_virtio_tx_frame[MK_NETWORK_VIRTIO_TX_FRAME_BYTES] __attribute__((aligned(16)));
static const uint8_t g_network_virtio_fallback_mac[6] = {0x52u, 0x54u, 0x00u, 0x12u, 0x34u, 0x56u};
static struct mk_network_virtio_link_state g_network_virtio_link;

struct mk_network_service_state {
    struct mk_network_info info;
    struct mk_network_status status;
    struct mk_network_scan_entry_state scans[4];
    uint32_t scan_count;
    uint32_t ethernet_transition_polls;
    int ethernet_pending;
    struct mk_network_pci_probe_state pci_probe;
    struct mk_network_socket_state sockets[MK_NETWORK_SOCKET_MAX];
};

static struct mk_message g_last_network_request;
static struct mk_message g_last_network_reply;
static struct mk_network_service_state g_network_state;
static struct mk_network_event_subscription g_network_event_subscribers[MK_NETWORK_EVENT_SUBSCRIBERS];
static uint32_t g_network_event_sequence = 0u;

static void mk_network_event_init_subscribers(void);
static struct mk_network_event_subscription *mk_network_find_subscription(const process_t *subscriber);
static struct mk_network_event_subscription *mk_network_alloc_subscription(process_t *subscriber);
static void mk_network_enqueue_event(struct mk_network_event_subscription *subscription,
                                     uint32_t event_type,
                                     int32_t handle,
                                     int32_t peer_handle,
                                     uint32_t link_state,
                                     uint32_t byte_count,
                                     uint32_t sequence);
static uint32_t mk_network_publish_event(uint32_t event_type,
                                         int32_t handle,
                                         int32_t peer_handle,
                                         uint32_t link_state,
                                         uint32_t byte_count);
static void mk_network_publish_status_event(void);

static void mk_network_event_init_subscribers(void) {
    uint32_t index;

    g_network_event_sequence = 0u;
    for (index = 0u; index < MK_NETWORK_EVENT_SUBSCRIBERS; ++index) {
        struct mk_network_event_subscription *subscription = &g_network_event_subscribers[index];

        memset(subscription, 0, sizeof(*subscription));
        kernel_mailbox_init(&subscription->mailbox,
                            subscription->events,
                            sizeof(subscription->events[0]),
                            MK_NETWORK_EVENT_QUEUE_SIZE,
                            KERNEL_MAILBOX_DROP_NEWEST,
                            TASK_WAIT_CLASS_NETWORK,
                            MK_SERVICE_NETWORK);
    }
}

static struct mk_network_event_subscription *mk_network_find_subscription(const process_t *subscriber) {
    uint32_t index;

    if (subscriber == 0 || subscriber->pid <= 0) {
        return 0;
    }

    for (index = 0u; index < MK_NETWORK_EVENT_SUBSCRIBERS; ++index) {
        struct mk_network_event_subscription *subscription = &g_network_event_subscribers[index];

        if (subscription->pid == subscriber->pid && subscription->process == subscriber) {
            return subscription;
        }
    }
    return 0;
}

static struct mk_network_event_subscription *mk_network_alloc_subscription(process_t *subscriber) {
    uint32_t index;

    if (subscriber == 0 || subscriber->pid <= 0) {
        return 0;
    }

    for (index = 0u; index < MK_NETWORK_EVENT_SUBSCRIBERS; ++index) {
        struct mk_network_event_subscription *subscription = &g_network_event_subscribers[index];

        if (subscription->pid <= 0 || subscription->process == 0 ||
            scheduler_find_task_by_pid(subscription->pid) == 0) {
            memset(subscription, 0, sizeof(*subscription));
            subscription->pid = subscriber->pid;
            subscription->process = subscriber;
            kernel_mailbox_init(&subscription->mailbox,
                                subscription->events,
                                sizeof(subscription->events[0]),
                                MK_NETWORK_EVENT_QUEUE_SIZE,
                                KERNEL_MAILBOX_DROP_NEWEST,
                                TASK_WAIT_CLASS_NETWORK,
                                MK_SERVICE_NETWORK);
            return subscription;
        }
    }
    return 0;
}

static void mk_network_enqueue_event(struct mk_network_event_subscription *subscription,
                                     uint32_t event_type,
                                     int32_t handle,
                                     int32_t peer_handle,
                                     uint32_t link_state,
                                     uint32_t byte_count,
                                     uint32_t sequence) {
    struct mk_network_event event;

    if (subscription == 0 || event_type == MK_NETWORK_EVENT_NONE) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.abi_version = 1u;
    event.event_type = event_type;
    event.handle = handle;
    event.peer_handle = peer_handle;
    event.sequence = sequence;
    event.link_state = link_state;
    event.byte_count = byte_count;
    event.dropped_events = kernel_mailbox_dropped(&subscription->mailbox);
    event.tick = kernel_timer_get_ticks();
    if (kernel_mailbox_try_send(&subscription->mailbox, &event) == 0) {
        kernel_mailbox_clear_dropped(&subscription->mailbox);
    }
}

static uint32_t mk_network_publish_event(uint32_t event_type,
                                         int32_t handle,
                                         int32_t peer_handle,
                                         uint32_t link_state,
                                         uint32_t byte_count) {
    uint32_t index;
    uint32_t sequence;

    if (event_type == MK_NETWORK_EVENT_NONE) {
        return 0u;
    }

    sequence = ++g_network_event_sequence;
    for (index = 0u; index < MK_NETWORK_EVENT_SUBSCRIBERS; ++index) {
        struct mk_network_event_subscription *subscription = &g_network_event_subscribers[index];

        if (subscription->pid <= 0 || subscription->process == 0) {
            continue;
        }
        if (scheduler_find_task_by_pid(subscription->pid) == 0) {
            memset(subscription, 0, sizeof(*subscription));
            continue;
        }
        mk_network_enqueue_event(subscription,
                                 event_type,
                                 handle,
                                 peer_handle,
                                 link_state,
                                 byte_count,
                                 sequence);
    }
    return sequence;
}

static void mk_network_publish_status_event(void) {
    (void)mk_network_publish_event(MK_NETWORK_EVENT_STATUS,
                                   0,
                                   0,
                                   g_network_state.status.link_state,
                                   0u);
}

static uint32_t mk_network_current_pid(void) {
    return scheduler_current_pid();
}

static int mk_network_prepare_request(struct mk_message *message,
                                      uint32_t type,
                                      const void *payload,
                                      size_t payload_size) {
    const struct mk_service_record *service;

    if (message == 0) {
        return -1;
    }

    service = mk_service_find_by_type(MK_SERVICE_NETWORK);
    if (service == 0) {
        return -1;
    }

    mk_message_init(message, type);
    message->source_pid = mk_network_current_pid();
    message->target_pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    return mk_message_set_payload(message, payload, payload_size);
}

static int mk_network_reply_result(struct mk_message *reply, int value) {
    struct mk_network_result payload;

    payload.value = value;
    return mk_message_set_payload(reply, &payload, sizeof(payload));
}

static int mk_network_reply_info(struct mk_message *reply) {
    return mk_message_set_payload(reply, &g_network_state.info, sizeof(g_network_state.info));
}

static int mk_network_copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst == 0 || dst_size == 0u) {
        return -1;
    }

    if (src == 0) {
        dst[0] = '\0';
        return 0;
    }

    strncpy(dst, src, dst_size - 1u);
    dst[dst_size - 1u] = '\0';
    return 0;
}

static int mk_network_string_eq(const char *a, const char *b) {
    size_t i = 0u;

    if (a == 0 || b == 0) {
        return 0;
    }
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return a[i] == b[i];
}

static int mk_network_socket_valid_domain(uint32_t domain) {
    return domain == AF_UNIX || domain == AF_LOCAL || domain == AF_INET;
}

static int mk_network_socket_valid_type(uint32_t type) {
    return type == SOCK_STREAM || type == SOCK_DGRAM;
}

static uint16_t mk_network_pci_io_base(uint32_t bar_value) {
    if ((bar_value & 0x1u) == 0u) {
        return 0u;
    }
    return (uint16_t)(bar_value & 0xfffcu);
}

static uint32_t mk_network_align_up_u32(uint32_t value, uint32_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

static uint8_t mk_network_virtio_read_status(uint16_t io_base) {
    return inb((uint16_t)(io_base + VIRTIO_CONFIG_DEVICE_STATUS));
}

static void mk_network_virtio_write_status(uint16_t io_base, uint8_t status) {
    outb((uint16_t)(io_base + VIRTIO_CONFIG_DEVICE_STATUS), status);
}

static void mk_network_virtio_set_status_bits(uint16_t io_base, uint8_t bits) {
    mk_network_virtio_write_status(io_base, (uint8_t)(mk_network_virtio_read_status(io_base) | bits));
}

static void mk_network_virtio_notify_queue(uint16_t io_base, uint16_t queue_index) {
    outw((uint16_t)(io_base + VIRTIO_CONFIG_QUEUE_NOTIFY), queue_index);
}

static void mk_network_virtio_ack_isr(uint16_t io_base) {
    (void)inb((uint16_t)(io_base + VIRTIO_CONFIG_ISR_STATUS));
}

static void mk_network_virtio_publish_desc_chain(struct mk_network_virtio_legacy_queue *queue_state,
                                                 uint16_t head_desc) {
    uint16_t slot;

    if (queue_state == 0 || queue_state->avail == 0 || queue_state->queue_size == 0u) {
        return;
    }

    slot = (uint16_t)(queue_state->next_avail_idx % queue_state->queue_size);
    queue_state->avail->ring[slot] = head_desc;
    __sync_synchronize();
    queue_state->next_avail_idx = (uint16_t)(queue_state->next_avail_idx + 1u);
    queue_state->avail->idx = queue_state->next_avail_idx;
}

static int mk_network_virtio_setup_queue(uint16_t io_base,
                                         uint16_t queue_index,
                                         uint16_t offered_size,
                                         struct mk_network_virtio_legacy_queue *queue_state) {
    uint32_t desc_bytes;
    uint32_t avail_bytes;
    uint32_t used_bytes;
    uint32_t used_offset;
    uint32_t total_bytes;
    uint8_t *storage;

    if (queue_state == 0 || offered_size == 0u || offered_size > MK_NETWORK_VIRTIO_MAX_QUEUE_SIZE) {
        return -1;
    }

    desc_bytes = (uint32_t)sizeof(struct mk_network_vring_desc) * (uint32_t)offered_size;
    avail_bytes = (uint32_t)(offsetof(struct mk_network_vring_avail, ring) +
                             ((uint32_t)offered_size * (uint32_t)sizeof(uint16_t)) +
                             sizeof(uint16_t));
    used_bytes = (uint32_t)(offsetof(struct mk_network_vring_used, ring) +
                            ((uint32_t)offered_size * (uint32_t)sizeof(struct mk_network_vring_used_elem)) +
                            sizeof(uint16_t));
    used_offset = mk_network_align_up_u32(desc_bytes + avail_bytes, MK_NETWORK_VIRTIO_RING_ALIGN);
    total_bytes = used_offset + used_bytes;
    if (total_bytes > sizeof(g_network_virtio_queue_storage[0])) {
        return -1;
    }

    storage = g_network_virtio_queue_storage[queue_index];
    memset(storage, 0, total_bytes);

    queue_state->desc = (struct mk_network_vring_desc *)storage;
    queue_state->avail = (struct mk_network_vring_avail *)(storage + desc_bytes);
    queue_state->used = (struct mk_network_vring_used *)(storage + used_offset);
    queue_state->queue_size = offered_size;
    queue_state->used_offset = used_offset;
    queue_state->total_bytes = total_bytes;
    queue_state->phys_addr = (uintptr_t)storage;
    queue_state->next_avail_idx = 0u;
    queue_state->last_used_idx = 0u;

    outw((uint16_t)(io_base + VIRTIO_CONFIG_QUEUE_SELECT), queue_index);
    outl((uint16_t)(io_base + VIRTIO_CONFIG_QUEUE_ADDRESS),
         (uint32_t)(queue_state->phys_addr / VIRTIO_PAGE_SIZE));
    return 0;
}

static int mk_network_virtio_frame_is_arp_reply(const uint8_t *frame, uint32_t payload_len) {
    if (frame == 0 || payload_len < 42u) {
        return 0;
    }
    return frame[12] == 0x08u &&
           frame[13] == 0x06u &&
           frame[20] == 0x00u &&
           frame[21] == 0x02u;
}

static void mk_network_virtio_handle_rx_frame(uint16_t head_desc,
                                              uint16_t slot,
                                              uint32_t payload_len) {
    g_network_virtio_link.rx_activity_seen = 1;
    g_network_virtio_link.rx_frames_completed += 1u;
    (void)mk_network_publish_event(MK_NETWORK_EVENT_BACKEND_RX,
                                   0,
                                   0,
                                   g_network_state.status.link_state,
                                   payload_len);

    if (!g_network_virtio_link.rx_activity_logged) {
        kernel_debug_printf("network: virtio rx activity len=%u head=%u\n",
                            (unsigned int)payload_len,
                            (unsigned int)head_desc);
        g_network_virtio_link.rx_activity_logged = 1;
    }

    if (slot < MK_NETWORK_VIRTIO_RX_SLOT_COUNT &&
        mk_network_virtio_frame_is_arp_reply(g_network_virtio_rx_payload[slot], payload_len)) {
        g_network_virtio_link.rx_arp_reply_seen = 1;
        if (!g_network_virtio_link.rx_arp_reply_logged) {
            kernel_debug_puts("network: virtio rx arp-reply seen\n");
            g_network_virtio_link.rx_arp_reply_logged = 1;
        }
    }
}

static int mk_network_virtio_submit_frame(struct mk_network_pci_probe_state *probe,
                                          const uint8_t *frame,
                                          uint32_t frame_len) {
    struct mk_network_virtio_legacy_queue *queue_state;

    if (probe == 0 || frame == 0 || frame_len == 0u || !probe->virtio_queue_ready) {
        return -1;
    }

    queue_state = &g_network_virtio_queues[MK_NETWORK_VIRTIO_TX_QUEUE_INDEX];
    if (queue_state->queue_size < 2u || g_network_virtio_link.tx_busy) {
        return -1;
    }
    if (frame_len > sizeof(g_network_virtio_tx_frame)) {
        return -1;
    }

    memset(&g_network_virtio_tx_hdr, 0, sizeof(g_network_virtio_tx_hdr));
    memset(g_network_virtio_tx_frame, 0, sizeof(g_network_virtio_tx_frame));
    memcpy(g_network_virtio_tx_frame, frame, frame_len);

    queue_state->desc[0].addr = (uint64_t)(uintptr_t)&g_network_virtio_tx_hdr;
    queue_state->desc[0].len = MK_NETWORK_VIRTIO_HDR_SIZE;
    queue_state->desc[0].flags = MK_NETWORK_VIRTIO_DESC_F_NEXT;
    queue_state->desc[0].next = 1u;
    queue_state->desc[1].addr = (uint64_t)(uintptr_t)&g_network_virtio_tx_frame[0];
    queue_state->desc[1].len = frame_len;
    queue_state->desc[1].flags = 0u;
    queue_state->desc[1].next = 0u;

    mk_network_virtio_publish_desc_chain(queue_state, 0u);
    g_network_virtio_link.tx_busy = 1;
    g_network_virtio_link.tx_frames_submitted += 1u;
    g_network_virtio_link.last_tx_frame_bytes = frame_len;
    mk_network_virtio_notify_queue(probe->io_base, MK_NETWORK_VIRTIO_TX_QUEUE_INDEX);
    return 0;
}

static void mk_network_virtio_post_rx_buffers(struct mk_network_pci_probe_state *probe) {
    struct mk_network_virtio_legacy_queue *queue_state;
    uint16_t slot;

    if (probe == 0 || !probe->virtio_queue_ready || g_network_virtio_link.rx_posted) {
        return;
    }

    queue_state = &g_network_virtio_queues[MK_NETWORK_VIRTIO_RX_QUEUE_INDEX];
    if (queue_state->queue_size < (MK_NETWORK_VIRTIO_RX_SLOT_COUNT * 2u)) {
        return;
    }

    for (slot = 0u; slot < MK_NETWORK_VIRTIO_RX_SLOT_COUNT; ++slot) {
        uint16_t head_desc = (uint16_t)(slot * 2u);
        uint16_t data_desc = (uint16_t)(head_desc + 1u);

        memset(&g_network_virtio_rx_hdr[slot], 0, sizeof(g_network_virtio_rx_hdr[slot]));
        memset(g_network_virtio_rx_payload[slot], 0, sizeof(g_network_virtio_rx_payload[slot]));

        queue_state->desc[head_desc].addr = (uint64_t)(uintptr_t)&g_network_virtio_rx_hdr[slot];
        queue_state->desc[head_desc].len = MK_NETWORK_VIRTIO_HDR_SIZE;
        queue_state->desc[head_desc].flags = MK_NETWORK_VIRTIO_DESC_F_NEXT | MK_NETWORK_VIRTIO_DESC_F_WRITE;
        queue_state->desc[head_desc].next = data_desc;

        queue_state->desc[data_desc].addr = (uint64_t)(uintptr_t)g_network_virtio_rx_payload[slot];
        queue_state->desc[data_desc].len = MK_NETWORK_VIRTIO_RX_BUFFER_BYTES;
        queue_state->desc[data_desc].flags = MK_NETWORK_VIRTIO_DESC_F_WRITE;
        queue_state->desc[data_desc].next = 0u;

        mk_network_virtio_publish_desc_chain(queue_state, head_desc);
    }

    g_network_virtio_link.rx_posted = 1;
    mk_network_virtio_notify_queue(probe->io_base, MK_NETWORK_VIRTIO_RX_QUEUE_INDEX);
}

static void mk_network_virtio_send_tx_smoke(struct mk_network_pci_probe_state *probe) {
    uint8_t *frame = g_network_virtio_tx_frame;
    const uint8_t *source_mac;

    if (probe == 0 || !probe->virtio_queue_ready || g_network_virtio_link.tx_smoke_sent) {
        return;
    }
    source_mac = probe->mac_valid ? probe->mac : g_network_virtio_fallback_mac;

    memset(frame, 0xff, 6u);
    memcpy(frame + 6u, source_mac, 6u);
    frame[12] = 0x08u;
    frame[13] = 0x06u;
    frame[14] = 0x00u;
    frame[15] = 0x01u;
    frame[16] = 0x08u;
    frame[17] = 0x00u;
    frame[18] = 0x06u;
    frame[19] = 0x04u;
    frame[20] = 0x00u;
    frame[21] = 0x01u;
    memcpy(frame + 22u, source_mac, 6u);
    frame[28] = 10u;
    frame[29] = 0u;
    frame[30] = 2u;
    frame[31] = 15u;
    memset(frame + 32u, 0, 6u);
    frame[38] = 10u;
    frame[39] = 0u;
    frame[40] = 2u;
    frame[41] = 2u;

    if (mk_network_virtio_submit_frame(probe, frame, 42u) == 0) {
        g_network_virtio_link.tx_smoke_sent = 1;
    }
}

static void mk_network_virtio_drain_tx_queue(struct mk_network_pci_probe_state *probe) {
    struct mk_network_virtio_legacy_queue *tx_queue;

    if (probe == 0 || !probe->virtio_queue_ready) {
        return;
    }

    tx_queue = &g_network_virtio_queues[MK_NETWORK_VIRTIO_TX_QUEUE_INDEX];
    while (tx_queue->last_used_idx != tx_queue->used->idx) {
        uint16_t used_slot = (uint16_t)(tx_queue->last_used_idx % tx_queue->queue_size);
        struct mk_network_vring_used_elem *elem = &tx_queue->used->ring[used_slot];

        tx_queue->last_used_idx = (uint16_t)(tx_queue->last_used_idx + 1u);
        g_network_virtio_link.tx_busy = 0;
        g_network_virtio_link.tx_frames_completed += 1u;
        (void)mk_network_publish_event(MK_NETWORK_EVENT_BACKEND_TX,
                                       0,
                                       0,
                                       g_network_state.status.link_state,
                                       g_network_virtio_link.last_tx_frame_bytes);
        if ((int)elem->id == 0) {
            g_network_virtio_link.tx_smoke_done = 1;
            if (!g_network_virtio_link.tx_smoke_logged) {
                kernel_debug_puts("network: virtio tx smoke consumed\n");
                g_network_virtio_link.tx_smoke_logged = 1;
            }
        }
    }
}

static void mk_network_virtio_drain_rx_queue(struct mk_network_pci_probe_state *probe) {
    struct mk_network_virtio_legacy_queue *rx_queue;

    if (probe == 0 || !probe->virtio_queue_ready) {
        return;
    }

    rx_queue = &g_network_virtio_queues[MK_NETWORK_VIRTIO_RX_QUEUE_INDEX];

    while (rx_queue->last_used_idx != rx_queue->used->idx) {
        uint16_t used_slot = (uint16_t)(rx_queue->last_used_idx % rx_queue->queue_size);
        struct mk_network_vring_used_elem *elem = &rx_queue->used->ring[used_slot];
        uint16_t head_desc = (uint16_t)elem->id;
        uint16_t slot = (uint16_t)(head_desc / 2u);
        uint32_t payload_len = elem->len > MK_NETWORK_VIRTIO_HDR_SIZE ?
            (uint32_t)(elem->len - MK_NETWORK_VIRTIO_HDR_SIZE) : 0u;

        rx_queue->last_used_idx = (uint16_t)(rx_queue->last_used_idx + 1u);
        mk_network_virtio_handle_rx_frame(head_desc, slot, payload_len);

        if (slot < MK_NETWORK_VIRTIO_RX_SLOT_COUNT) {
            mk_network_virtio_publish_desc_chain(rx_queue, head_desc);
        }
    }

    if (g_network_virtio_link.rx_activity_seen) {
        mk_network_virtio_notify_queue(probe->io_base, MK_NETWORK_VIRTIO_RX_QUEUE_INDEX);
    }
}

static void mk_network_virtio_poll_queues(struct mk_network_pci_probe_state *probe) {
    if (probe == 0 || !probe->virtio_queue_ready) {
        return;
    }

    mk_network_virtio_ack_isr(probe->io_base);
    mk_network_virtio_drain_tx_queue(probe);
    mk_network_virtio_drain_rx_queue(probe);
}

static void mk_network_virtio_spin_poll(struct mk_network_pci_probe_state *probe, uint32_t iterations) {
    uint32_t i;

    for (i = 0u; i < iterations; ++i) {
        mk_network_virtio_poll_queues(probe);
        if (g_network_virtio_link.tx_smoke_done &&
            (g_network_virtio_link.rx_activity_seen || i >= (iterations / 4u))) {
            break;
        }
    }
}

static void mk_network_virtio_maybe_activate_runtime(struct mk_network_pci_probe_state *probe) {
    if (probe == 0 || !probe->virtio_queue_ready) {
        return;
    }
    if (!g_network_virtio_link.rx_posted) {
        mk_network_virtio_post_rx_buffers(probe);
    }
    if (!g_network_virtio_link.tx_smoke_sent) {
        mk_network_virtio_send_tx_smoke(probe);
    }
    if (g_network_virtio_link.tx_smoke_sent &&
        (!g_network_virtio_link.tx_smoke_done || !g_network_virtio_link.rx_activity_seen)) {
        mk_network_virtio_spin_poll(probe, 4096u);
    }
}

static void mk_network_probe_virtio_legacy_queues(struct mk_network_pci_probe_state *probe) {
    uint16_t queue_index;
    uint32_t guest_features;

    if (probe == 0 || probe->io_base == 0u) {
        return;
    }

    mk_network_virtio_write_status(probe->io_base, VIRTIO_CONFIG_DEVICE_STATUS_RESET);
    if (mk_network_virtio_read_status(probe->io_base) != VIRTIO_CONFIG_DEVICE_STATUS_RESET) {
        return;
    }

    mk_network_virtio_set_status_bits(probe->io_base, VIRTIO_CONFIG_DEVICE_STATUS_ACK);
    mk_network_virtio_set_status_bits(probe->io_base, VIRTIO_CONFIG_DEVICE_STATUS_DRIVER);

    guest_features = 0u;
    if (probe->mac_valid) {
        guest_features |= VIRTIO_NET_F_MAC;
    }
    if (probe->virtio_status_valid) {
        guest_features |= VIRTIO_NET_F_STATUS;
    }
    outl((uint16_t)(probe->io_base + VIRTIO_CONFIG_GUEST_FEATURES), guest_features);

    memset(g_network_virtio_queues, 0, sizeof(g_network_virtio_queues));
    memset(&g_network_virtio_link, 0, sizeof(g_network_virtio_link));
    for (queue_index = 0u; queue_index < MK_NETWORK_VIRTIO_QUEUE_COUNT; ++queue_index) {
        uint16_t queue_size;

        outw((uint16_t)(probe->io_base + VIRTIO_CONFIG_QUEUE_SELECT), queue_index);
        queue_size = inw((uint16_t)(probe->io_base + VIRTIO_CONFIG_QUEUE_SIZE));
        probe->queue_size[queue_index] = queue_size;
        if (mk_network_virtio_setup_queue(probe->io_base,
                                          queue_index,
                                          queue_size,
                                          &g_network_virtio_queues[queue_index]) != 0) {
            mk_network_virtio_set_status_bits(probe->io_base, VIRTIO_CONFIG_DEVICE_STATUS_FAILED);
            return;
        }
    }

    mk_network_virtio_set_status_bits(probe->io_base, VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK);
    probe->virtio_queue_ready = 1;
    mk_network_virtio_post_rx_buffers(probe);
}

static void mk_network_reset_link_state(void) {
    g_network_state.ethernet_pending = 0;
    g_network_state.ethernet_transition_polls = 0u;
    memset(&g_network_state.status, 0, sizeof(g_network_state.status));
    g_network_state.status.active_kind = MK_NETWORK_IF_LOOPBACK;
    g_network_state.status.visible_network_count = g_network_state.scan_count;
    (void)mk_network_copy_string(
        g_network_state.status.active_if,
        sizeof(g_network_state.status.active_if),
        g_network_state.pci_probe.present ? g_network_state.pci_probe.if_name : "lo0");
    mk_network_publish_status_event();
}

static void mk_network_probe_virtio_legacy_config(struct mk_network_pci_probe_state *probe) {
    uint32_t device_features;
    uint16_t config_base;
    uint32_t i;

    if (probe == 0 || probe->vendor_id != VIRTIO_PCI_VENDOR_ID) {
        return;
    }
    if (probe->device_id != VIRTIO_PCI_DEVICE_NET_LEGACY &&
        probe->device_id != VIRTIO_PCI_DEVICE_NET_MODERN) {
        return;
    }

    probe->io_base = mk_network_pci_io_base(
        kernel_pci_config_read_u32(probe->bus, probe->slot, probe->function, 0x10u));
    if (probe->io_base == 0u) {
        return;
    }

    device_features = inl((uint16_t)(probe->io_base + VIRTIO_CONFIG_DEVICE_FEATURES));
    config_base = (uint16_t)(probe->io_base + VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI);

    if ((device_features & VIRTIO_NET_F_MAC) != 0u) {
        for (i = 0u; i < 6u; ++i) {
            probe->mac[i] = inb((uint16_t)(config_base + VIRTIO_NET_CONFIG_MAC + i));
        }
        probe->mac_valid = 1;
    }
    if ((device_features & VIRTIO_NET_F_STATUS) != 0u) {
        uint16_t status = inw((uint16_t)(config_base + VIRTIO_NET_CONFIG_STATUS));

        probe->virtio_status_valid = 1;
        probe->virtio_link_up = (status & VIRTIO_NET_S_LINK_UP) != 0u;
    }

    probe->virtio_legacy_ready = 1;
    mk_network_probe_virtio_legacy_queues(probe);
}

static void mk_network_select_pci_backend(const struct kernel_pci_device_info *info,
                                          struct mk_network_pci_probe_state *probe) {
    const char *if_name = "eth0";
    const char *backend_name = "compat-pci-pending";

    if (info == 0 || probe == 0) {
        return;
    }

    if (info->vendor_id == 0x1AF4u) {
        if_name = "vio0";
        backend_name = "compat-virtio";
    } else if (info->vendor_id == 0x8086u) {
        if_name = "em0";
        backend_name = "compat-em";
    } else if (info->vendor_id == 0x10ECu) {
        if (info->device_id == 0x8029u) {
            if_name = "ne0";
            backend_name = "compat-ne2000";
        } else {
            if_name = "re0";
            backend_name = "compat-rtl81x9";
        }
    } else if (info->vendor_id == 0x1022u && info->device_id == 0x2000u) {
        if_name = "pcn0";
        backend_name = "compat-pcnet";
    }

    probe->present = 1;
    probe->bus = info->bus;
    probe->slot = info->slot;
    probe->function = info->function;
    probe->vendor_id = info->vendor_id;
    probe->device_id = info->device_id;
    (void)mk_network_copy_string(probe->if_name, sizeof(probe->if_name), if_name);
    (void)mk_network_copy_string(probe->backend_name, sizeof(probe->backend_name), backend_name);
    mk_network_probe_virtio_legacy_config(probe);
}

static int mk_network_probe_pci_cb(const struct kernel_pci_device_info *info, void *ctx) {
    struct mk_network_pci_probe_state *probe = (struct mk_network_pci_probe_state *)ctx;

    if (info == 0 || probe == 0 || probe->present) {
        return 0;
    }
    if (info->class_code != 0x02u) {
        return 0;
    }
    if (info->subclass != 0x00u && info->subclass != 0x80u) {
        return 0;
    }

    mk_network_select_pci_backend(info, probe);
    return 1;
}

static void mk_network_probe_pci_devices(void) {
    memset(&g_network_state.pci_probe, 0, sizeof(g_network_state.pci_probe));
    (void)kernel_pci_enumerate(mk_network_probe_pci_cb, &g_network_state.pci_probe);
}

static void mk_network_log_probe(void) {
    if (!g_network_state.pci_probe.present) {
        return;
    }
    if (g_network_state.pci_probe.virtio_legacy_ready) {
        kernel_debug_printf("network: pci=%x:%x if=%s backend=%s io=%x q=%d rxq=%d txq=%d txsmoke=%d rxseen=%d arprx=%d txf=%d rxf=%d link=%d mac=%x:%x:%x:%x:%x:%x\n",
                            (unsigned int)g_network_state.pci_probe.vendor_id,
                            (unsigned int)g_network_state.pci_probe.device_id,
                            g_network_state.pci_probe.if_name,
                            g_network_state.pci_probe.backend_name,
                            (unsigned int)g_network_state.pci_probe.io_base,
                            g_network_state.pci_probe.virtio_queue_ready,
                            (int)g_network_state.pci_probe.queue_size[0],
                            (int)g_network_state.pci_probe.queue_size[1],
                            g_network_virtio_link.tx_smoke_done,
                            g_network_virtio_link.rx_activity_seen,
                            g_network_virtio_link.rx_arp_reply_seen,
                            (unsigned int)g_network_virtio_link.tx_frames_completed,
                            (unsigned int)g_network_virtio_link.rx_frames_completed,
                            g_network_state.pci_probe.virtio_status_valid ?
                                g_network_state.pci_probe.virtio_link_up : 0,
                            (unsigned int)g_network_state.pci_probe.mac[0],
                            (unsigned int)g_network_state.pci_probe.mac[1],
                            (unsigned int)g_network_state.pci_probe.mac[2],
                            (unsigned int)g_network_state.pci_probe.mac[3],
                            (unsigned int)g_network_state.pci_probe.mac[4],
                            (unsigned int)g_network_state.pci_probe.mac[5]);
        return;
    }

    kernel_debug_printf("network: pci=%x:%x if=%s backend=%s\n",
                        (unsigned int)g_network_state.pci_probe.vendor_id,
                        (unsigned int)g_network_state.pci_probe.device_id,
                        g_network_state.pci_probe.if_name,
                        g_network_state.pci_probe.backend_name);
}

static struct mk_network_socket_state *mk_network_socket_by_handle(int handle) {
    if (handle <= 0 || handle > MK_NETWORK_SOCKET_MAX) {
        return 0;
    }
    if (!g_network_state.sockets[handle - 1].used) {
        return 0;
    }
    return &g_network_state.sockets[handle - 1];
}

static struct mk_network_socket_state *mk_network_socket_by_handle_for_current_pid(int handle) {
    struct mk_network_socket_state *socket_state = mk_network_socket_by_handle(handle);

    if (socket_state == 0 || socket_state->owner_pid != mk_network_current_pid()) {
        return 0;
    }
    return socket_state;
}

static int mk_network_socket_address_match(const struct mk_network_socket_state *socket_state,
                                           const struct sockaddr *address,
                                           uint32_t address_length) {
    if (socket_state == 0 || address == 0) {
        return 0;
    }
    if (!socket_state->bound || socket_state->address_length != address_length) {
        return 0;
    }
    return memcmp(&socket_state->address, address, address_length) == 0;
}

static void mk_network_socket_disconnect_peer(struct mk_network_socket_state *socket_state) {
    struct mk_network_socket_state *peer;

    if (socket_state == 0 || !socket_state->connected || socket_state->peer_handle <= 0) {
        return;
    }

    peer = mk_network_socket_by_handle(socket_state->peer_handle);
    if (peer != 0 && peer->peer_handle == (int)(socket_state - g_network_state.sockets) + 1) {
        peer->connected = 0;
        peer->peer_handle = -1;
    }
    socket_state->connected = 0;
    socket_state->peer_handle = -1;
}

static struct mk_network_socket_state *mk_network_socket_allocate(uint32_t owner_pid,
                                                                  uint32_t domain,
                                                                  uint32_t type,
                                                                  uint32_t protocol,
                                                                  int *handle_out) {
    uint32_t index;

    for (index = 0u; index < MK_NETWORK_SOCKET_MAX; ++index) {
        struct mk_network_socket_state *socket_state = &g_network_state.sockets[index];

        if (socket_state->used) {
            continue;
        }
        memset(socket_state, 0, sizeof(*socket_state));
        socket_state->used = 1;
        socket_state->owner_pid = owner_pid;
        socket_state->domain = domain;
        socket_state->type = type;
        socket_state->protocol = protocol;
        socket_state->peer_handle = -1;
        socket_state->pending_accept_handle = -1;
        if (handle_out != 0) {
            *handle_out = (int)index + 1;
        }
        return socket_state;
    }

    return 0;
}

static void mk_network_apply_ethernet_profile(void) {
    if (!g_network_state.pci_probe.present) {
        mk_network_reset_link_state();
        return;
    }

    g_network_state.ethernet_pending = 0;
    g_network_state.ethernet_transition_polls = 0u;
    g_network_state.status.link_state =
        (g_network_state.pci_probe.virtio_status_valid && !g_network_state.pci_probe.virtio_link_up)
            ? MK_NETWORK_LINK_DISCONNECTED
            : MK_NETWORK_LINK_CONNECTED;
    g_network_state.status.active_kind = MK_NETWORK_IF_ETHERNET;
    g_network_state.status.wifi_signal = 0u;
    g_network_state.status.visible_network_count = g_network_state.scan_count;
    (void)mk_network_copy_string(g_network_state.status.active_if,
                                 sizeof(g_network_state.status.active_if),
                                 g_network_state.pci_probe.if_name);
    g_network_state.status.current_ssid[0] = '\0';
    if (!g_network_state.pci_probe.virtio_legacy_ready) {
        (void)mk_network_copy_string(g_network_state.status.ip_address,
                                     sizeof(g_network_state.status.ip_address),
                                     "10.0.2.15");
        (void)mk_network_copy_string(g_network_state.status.gateway,
                                     sizeof(g_network_state.status.gateway),
                                     "10.0.2.2");
        (void)mk_network_copy_string(g_network_state.status.dns_server,
                                     sizeof(g_network_state.status.dns_server),
                                     "10.0.2.3");
    } else {
        g_network_state.status.ip_address[0] = '\0';
        g_network_state.status.gateway[0] = '\0';
        g_network_state.status.dns_server[0] = '\0';
    }
    mk_network_publish_status_event();
}

static int mk_network_apply_ethernet_config(const struct mk_network_ethernet_config *config) {
    if (config == 0 ||
        !g_network_state.pci_probe.present ||
        !mk_network_string_eq(config->if_name, g_network_state.pci_probe.if_name)) {
        return -1;
    }

    g_network_state.ethernet_pending = 0;
    g_network_state.ethernet_transition_polls = 0u;
    g_network_state.status.link_state = MK_NETWORK_LINK_CONNECTED;
    g_network_state.status.active_kind = MK_NETWORK_IF_ETHERNET;
    g_network_state.status.wifi_signal = 0u;
    g_network_state.status.visible_network_count = g_network_state.scan_count;
    (void)mk_network_copy_string(g_network_state.status.active_if,
                                 sizeof(g_network_state.status.active_if),
                                 config->if_name);
    g_network_state.status.current_ssid[0] = '\0';
    (void)mk_network_copy_string(g_network_state.status.ip_address,
                                 sizeof(g_network_state.status.ip_address),
                                 config->ip_address);
    (void)mk_network_copy_string(g_network_state.status.gateway,
                                 sizeof(g_network_state.status.gateway),
                                 config->gateway);
    (void)mk_network_copy_string(g_network_state.status.dns_server,
                                 sizeof(g_network_state.status.dns_server),
                                 config->dns_server);
    mk_network_publish_status_event();
    return 0;
}

static void mk_network_begin_ethernet_connect(void) {
    if (!g_network_state.pci_probe.present) {
        mk_network_reset_link_state();
        return;
    }

    g_network_state.ethernet_pending = 1;
    g_network_state.ethernet_transition_polls = 2u;
    g_network_state.status.link_state = MK_NETWORK_LINK_CONNECTING;
    g_network_state.status.active_kind = MK_NETWORK_IF_ETHERNET;
    g_network_state.status.wifi_signal = 0u;
    g_network_state.status.visible_network_count = g_network_state.scan_count;
    (void)mk_network_copy_string(g_network_state.status.active_if,
                                 sizeof(g_network_state.status.active_if),
                                 g_network_state.pci_probe.if_name);
    g_network_state.status.current_ssid[0] = '\0';
    g_network_state.status.ip_address[0] = '\0';
    g_network_state.status.gateway[0] = '\0';
    g_network_state.status.dns_server[0] = '\0';
    mk_network_publish_status_event();
}

static void mk_network_progress_state(void) {
    mk_network_virtio_maybe_activate_runtime(&g_network_state.pci_probe);
    mk_network_virtio_poll_queues(&g_network_state.pci_probe);
    if (!g_network_state.ethernet_pending) {
        return;
    }
    if (g_network_state.ethernet_transition_polls > 0u) {
        g_network_state.ethernet_transition_polls -= 1u;
        if (g_network_state.ethernet_transition_polls > 0u) {
            return;
        }
    }
    mk_network_apply_ethernet_profile();
}

static int mk_network_fill_scan_info(uint32_t index, struct mk_network_scan_info *info) {
    if (info == 0 || index >= g_network_state.scan_count) {
        return -1;
    }

    memset(info, 0, sizeof(*info));
    info->index = index;
    info->signal_strength = g_network_state.scans[index].signal_strength;
    info->security = g_network_state.scans[index].security;
    info->connected = g_network_state.status.active_kind == MK_NETWORK_IF_WIFI &&
                      mk_network_string_eq(g_network_state.status.current_ssid,
                                           g_network_state.scans[index].ssid);
    (void)mk_network_copy_string(info->if_name, sizeof(info->if_name), "wlan0");
    (void)mk_network_copy_string(info->ssid, sizeof(info->ssid), g_network_state.scans[index].ssid);
    return 0;
}

static int mk_network_state_connect_wifi(const struct mk_network_connect_request *request) {
    uint32_t index;

    if (request == 0 || !mk_network_string_eq(request->if_name, "wlan0")) {
        return -1;
    }

    for (index = 0u; index < g_network_state.scan_count; ++index) {
        if (!mk_network_string_eq(g_network_state.scans[index].ssid, request->ssid)) {
            continue;
        }
        if (g_network_state.scans[index].security == MK_NETWORK_SECURITY_WPA_PSK &&
            request->psk[0] == '\0') {
            return -1;
        }

        g_network_state.ethernet_pending = 0;
        g_network_state.ethernet_transition_polls = 0u;
        g_network_state.status.link_state = MK_NETWORK_LINK_CONNECTED;
        g_network_state.status.active_kind = MK_NETWORK_IF_WIFI;
        g_network_state.status.wifi_signal = g_network_state.scans[index].signal_strength;
        g_network_state.status.visible_network_count = g_network_state.scan_count;
        (void)mk_network_copy_string(g_network_state.status.active_if,
                                     sizeof(g_network_state.status.active_if),
                                     "wlan0");
        (void)mk_network_copy_string(g_network_state.status.current_ssid,
                                     sizeof(g_network_state.status.current_ssid),
                                     g_network_state.scans[index].ssid);
        (void)mk_network_copy_string(g_network_state.status.ip_address,
                                     sizeof(g_network_state.status.ip_address),
                                     "192.168.1.24");
        (void)mk_network_copy_string(g_network_state.status.gateway,
                                     sizeof(g_network_state.status.gateway),
                                     "192.168.1.1");
        (void)mk_network_copy_string(g_network_state.status.dns_server,
                                     sizeof(g_network_state.status.dns_server),
                                     "1.1.1.1");
        mk_network_publish_status_event();
        return 0;
    }

    return -1;
}

static int mk_network_state_disconnect(const char *if_name) {
    if (if_name == 0) {
        return -1;
    }

    if (mk_network_string_eq(if_name, "wlan0")) {
        if (g_network_state.pci_probe.present) {
            mk_network_begin_ethernet_connect();
        } else {
            mk_network_reset_link_state();
        }
        return 0;
    }
    if (g_network_state.pci_probe.present &&
        mk_network_string_eq(if_name, g_network_state.pci_probe.if_name)) {
        mk_network_reset_link_state();
        return 0;
    }
    return -1;
}

static int mk_network_state_connect_ethernet(const char *if_name) {
    if (if_name == 0 ||
        !g_network_state.pci_probe.present ||
        !mk_network_string_eq(if_name, g_network_state.pci_probe.if_name)) {
        return -1;
    }
    if (g_network_state.status.link_state == MK_NETWORK_LINK_CONNECTED &&
        g_network_state.status.active_kind == MK_NETWORK_IF_ETHERNET) {
        return 0;
    }
    mk_network_begin_ethernet_connect();
    return 0;
}

static int mk_network_local_handler(const struct mk_message *request,
                                    struct mk_message *reply,
                                    void *context) {
    (void)context;
    if (request == 0 || reply == 0) {
        return -1;
    }

    g_last_network_request = *request;
    mk_message_init(reply, request->type);
    reply->source_pid = request->target_pid;
    reply->target_pid = request->source_pid;

    switch (request->type) {
    case MK_MSG_HELLO:
    case MK_MSG_NET_GETINFO:
        if (request->payload_size != 0u) {
            return -1;
        }
        if (mk_network_reply_info(reply) != 0) {
            return -1;
        }
        g_last_network_reply = *reply;
        return 0;
    case MK_MSG_NET_GET_STATUS:
        if (request->payload_size != 0u) {
            return -1;
        }
        mk_network_progress_state();
        if (mk_message_set_payload(reply, &g_network_state.status, sizeof(g_network_state.status)) != 0) {
            return -1;
        }
        g_last_network_reply = *reply;
        return 0;
    case MK_MSG_NET_SCAN:
        if (request->payload_size != sizeof(struct mk_network_scan_request)) {
            return -1;
        }
        {
            struct mk_network_scan_request scan_request;
            struct mk_network_scan_info info;

            memcpy(&scan_request, request->payload, sizeof(scan_request));
            if (!mk_network_string_eq(scan_request.if_name, "wlan0") ||
                mk_network_fill_scan_info(scan_request.index, &info) != 0) {
                if (mk_network_reply_result(reply, -1) != 0) {
                    return -1;
                }
                g_last_network_reply = *reply;
                return 0;
            }
            if (mk_message_set_payload(reply, &info, sizeof(info)) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
    case MK_MSG_NET_CONNECT_WIFI:
        if (request->payload_size != sizeof(struct mk_network_connect_request)) {
            return -1;
        }
        if (mk_network_state_connect_wifi((const struct mk_network_connect_request *)request->payload) != 0) {
            if (mk_network_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_NET_DISCONNECT:
        if (request->payload_size != sizeof(struct mk_network_disconnect_request)) {
            return -1;
        }
        if (mk_network_state_disconnect(((const struct mk_network_disconnect_request *)request->payload)->if_name) != 0) {
            if (mk_network_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_NET_CONNECT_ETHERNET:
        if (request->payload_size != sizeof(struct mk_network_disconnect_request)) {
            return -1;
        }
        if (mk_network_state_connect_ethernet(((const struct mk_network_disconnect_request *)request->payload)->if_name) != 0) {
            if (mk_network_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_NET_CONFIGURE_ETHERNET:
        if (request->payload_size != sizeof(struct mk_network_ethernet_config)) {
            return -1;
        }
        if (mk_network_apply_ethernet_config((const struct mk_network_ethernet_config *)request->payload) != 0) {
            if (mk_network_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_NET_SOCKET:
        if (request->payload_size != sizeof(struct mk_network_socket_request)) {
            return -1;
        }
        break;
    case MK_MSG_NET_BIND:
    case MK_MSG_NET_CONNECT:
        if (request->payload_size != sizeof(struct mk_network_name_request)) {
            return -1;
        }
        break;
    case MK_MSG_NET_SEND:
    case MK_MSG_NET_RECV:
        if (request->payload_size != sizeof(struct mk_network_io_request)) {
            return -1;
        }
        break;
    case MK_MSG_NET_SETSOCKOPT:
    case MK_MSG_NET_GETSOCKOPT:
        if (request->payload_size != sizeof(struct mk_network_option_request)) {
            return -1;
        }
        break;
    default:
        return -1;
    }

    if (mk_network_reply_result(reply, -1) != 0) {
        return -1;
    }
    g_last_network_reply = *reply;
    return 0;
}

void mk_network_service_init(void) {
    memset(&g_last_network_request, 0, sizeof(g_last_network_request));
    memset(&g_last_network_reply, 0, sizeof(g_last_network_reply));
    memset(&g_network_state, 0, sizeof(g_network_state));
    mk_network_event_init_subscribers();

    mk_network_probe_pci_devices();

    g_network_state.info.flags = MK_NETWORK_CAPS_BSD_SOCKET_ABI |
                                 MK_NETWORK_CAPS_CONTROL_PLANE |
                                 MK_NETWORK_CAPS_LOOPBACK_READY |
                                 MK_NETWORK_CAPS_ETHERNET_STATUS |
                                 MK_NETWORK_CAPS_WIFI_SCAN |
                                 MK_NETWORK_CAPS_WIFI_CONNECT |
                                 MK_NETWORK_CAPS_DNS_STATUS;
    if (g_network_state.pci_probe.present) {
        g_network_state.info.flags |= MK_NETWORK_CAPS_DRIVER_EXTRACTION_PENDING;
    } else {
        g_network_state.info.flags |= MK_NETWORK_CAPS_QUERY_ONLY;
    }
    g_network_state.info.supported_families = MK_NETWORK_FAMILY_UNIX |
                                              MK_NETWORK_FAMILY_INET |
                                              MK_NETWORK_FAMILY_INET6;
    g_network_state.info.supported_socket_types = MK_NETWORK_SOCKET_STREAM |
                                                  MK_NETWORK_SOCKET_DGRAM;
    g_network_state.info.max_sockets = MK_NETWORK_SOCKET_MAX;
    g_network_state.info.max_packet_size = 1500u;

    g_network_state.scan_count = 4u;
    (void)mk_network_copy_string(g_network_state.scans[0].ssid, sizeof(g_network_state.scans[0].ssid), "VibeNet");
    g_network_state.scans[0].signal_strength = 4u;
    g_network_state.scans[0].security = MK_NETWORK_SECURITY_WPA_PSK;
    (void)mk_network_copy_string(g_network_state.scans[1].ssid, sizeof(g_network_state.scans[1].ssid), "Laboratorio");
    g_network_state.scans[1].signal_strength = 3u;
    g_network_state.scans[1].security = MK_NETWORK_SECURITY_WPA_PSK;
    (void)mk_network_copy_string(g_network_state.scans[2].ssid, sizeof(g_network_state.scans[2].ssid), "Visitantes");
    g_network_state.scans[2].signal_strength = 2u;
    g_network_state.scans[2].security = MK_NETWORK_SECURITY_OPEN;
    (void)mk_network_copy_string(g_network_state.scans[3].ssid, sizeof(g_network_state.scans[3].ssid), "CompatLab");
    g_network_state.scans[3].signal_strength = 4u;
    g_network_state.scans[3].security = MK_NETWORK_SECURITY_WPA_PSK;
    mk_network_apply_ethernet_profile();
    mk_network_log_probe();

    (void)mk_service_launch_task(MK_SERVICE_NETWORK,
                                 "network",
                                 mk_network_local_handler,
                                 0,
                                 userland_service_entry,
                                 8192u,
                                 MK_LAUNCH_FLAG_BOOTSTRAP |
                                 MK_LAUNCH_FLAG_BUILTIN);
}

int mk_network_service_ready(void) {
    return mk_service_find_by_type(MK_SERVICE_NETWORK) != 0;
}

int mk_network_service_get_info(struct mk_network_info *info) {
    struct mk_message request;
    struct mk_message reply;

    if (info == 0) {
        return -1;
    }

    if (mk_network_prepare_request(&request, MK_MSG_NET_GETINFO, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(*info)) {
        return -1;
    }
    memcpy(info, reply.payload, sizeof(*info));
    return 0;
}

int mk_network_service_get_status(struct mk_network_status *status) {
    struct mk_message request;
    struct mk_message reply;

    if (status == 0) {
        return -1;
    }

    if (mk_network_prepare_request(&request, MK_MSG_NET_GET_STATUS, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(*status)) {
        return -1;
    }
    memcpy(status, reply.payload, sizeof(*status));
    return 0;
}

int mk_network_service_get_scan(uint32_t index, struct mk_network_scan_info *info) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_network_scan_request scan_request;

    if (info == 0) {
        return -1;
    }

    memset(&scan_request, 0, sizeof(scan_request));
    scan_request.index = index;
    (void)mk_network_copy_string(scan_request.if_name, sizeof(scan_request.if_name), "wlan0");

    if (mk_network_prepare_request(&request, MK_MSG_NET_SCAN, &scan_request, sizeof(scan_request)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size == sizeof(struct mk_network_result)) {
        struct mk_network_result result;

        memcpy(&result, reply.payload, sizeof(result));
        return result.value;
    }
    if (reply.payload_size != sizeof(*info)) {
        return -1;
    }
    memcpy(info, reply.payload, sizeof(*info));
    return 0;
}

int mk_network_service_connect_wifi(const struct mk_network_connect_request *request) {
    struct mk_message message;
    struct mk_message reply;
    struct mk_network_result result;

    if (request == 0) {
        return -1;
    }

    if (mk_network_prepare_request(&message, MK_MSG_NET_CONNECT_WIFI, request, sizeof(*request)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &message, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_network_service_connect_ethernet(const char *if_name) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_network_disconnect_request connect_request;
    struct mk_network_result result;

    if (if_name == 0) {
        return -1;
    }

    memset(&connect_request, 0, sizeof(connect_request));
    (void)mk_network_copy_string(connect_request.if_name,
                                 sizeof(connect_request.if_name),
                                 if_name);

    if (mk_network_prepare_request(&request,
                                   MK_MSG_NET_CONNECT_ETHERNET,
                                   &connect_request,
                                   sizeof(connect_request)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_network_service_configure_ethernet(const struct mk_network_ethernet_config *config) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_network_result result;

    if (config == 0) {
        return -1;
    }

    if (mk_network_prepare_request(&request,
                                   MK_MSG_NET_CONFIGURE_ETHERNET,
                                   config,
                                   sizeof(*config)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_network_service_disconnect(const char *if_name) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_network_disconnect_request disconnect_request;
    struct mk_network_result result;

    if (if_name == 0) {
        return -1;
    }

    memset(&disconnect_request, 0, sizeof(disconnect_request));
    (void)mk_network_copy_string(disconnect_request.if_name,
                                 sizeof(disconnect_request.if_name),
                                 if_name);

    if (mk_network_prepare_request(&request,
                                   MK_MSG_NET_DISCONNECT,
                                   &disconnect_request,
                                   sizeof(disconnect_request)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_network_service_socket(uint32_t domain, uint32_t type, uint32_t protocol) {
    int handle;

    if (!mk_network_socket_valid_domain(domain) || !mk_network_socket_valid_type(type)) {
        return -1;
    }
    return mk_network_socket_allocate(mk_network_current_pid(), domain, type, protocol, &handle) != 0
               ? handle
               : -1;
}

int mk_network_service_bind(int handle, const struct sockaddr *address, uint32_t address_length) {
    uint32_t index;
    struct mk_network_socket_state *socket_state;

    if (address == 0 || address_length == 0u || address_length > MK_NETWORK_SOCKET_ADDRESS_MAX) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0 || socket_state->connected || socket_state->bound) {
        return -1;
    }
    if ((uint32_t)address->sa_family != socket_state->domain) {
        return -1;
    }

    for (index = 0u; index < MK_NETWORK_SOCKET_MAX; ++index) {
        struct mk_network_socket_state *other = &g_network_state.sockets[index];

        if (!other->used || other == socket_state) {
            continue;
        }
        if (other->domain != socket_state->domain || other->type != socket_state->type) {
            continue;
        }
        if (mk_network_socket_address_match(other, address, address_length)) {
            return -1;
        }
    }

    memset(&socket_state->address, 0, sizeof(socket_state->address));
    memcpy(&socket_state->address, address, address_length);
    socket_state->address_length = address_length;
    socket_state->bound = 1;
    return 0;
}

int mk_network_service_socket_connect(int handle, const struct sockaddr *address, uint32_t address_length) {
    uint32_t index;
    struct mk_network_socket_state *socket_state;

    if (address == 0 || address_length == 0u || address_length > MK_NETWORK_SOCKET_ADDRESS_MAX) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0 || socket_state->connected) {
        return -1;
    }
    if ((uint32_t)address->sa_family != socket_state->domain) {
        return -1;
    }

    for (index = 0u; index < MK_NETWORK_SOCKET_MAX; ++index) {
        struct mk_network_socket_state *peer = &g_network_state.sockets[index];
        int accepted_handle;
        struct mk_network_socket_state *accepted_socket;

        if (!peer->used || peer == socket_state) {
            continue;
        }
        if (peer->domain != socket_state->domain || peer->type != socket_state->type) {
            continue;
        }
        if (!mk_network_socket_address_match(peer, address, address_length)) {
            continue;
        }
        if (peer->listening) {
            if (peer->pending_accept_handle > 0) {
                return -1;
            }
            accepted_socket = mk_network_socket_allocate(peer->owner_pid,
                                                         peer->domain,
                                                         peer->type,
                                                         peer->protocol,
                                                         &accepted_handle);
            if (accepted_socket == 0) {
                return -1;
            }
            accepted_socket->bound = 1;
            accepted_socket->address_length = peer->address_length;
            memcpy(&accepted_socket->address, &peer->address, peer->address_length);
            accepted_socket->connected = 1;
            accepted_socket->peer_handle = handle;
            socket_state->connected = 1;
            socket_state->peer_handle = accepted_handle;
            peer->pending_accept_handle = accepted_handle;
            (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_ACCEPT,
                                           (int32_t)((peer - g_network_state.sockets) + 1),
                                           accepted_handle,
                                           g_network_state.status.link_state,
                                           0u);
            return 0;
        }
        if (peer->connected) {
            continue;
        }
        socket_state->connected = 1;
        socket_state->peer_handle = (int)index + 1;
        peer->connected = 1;
        peer->peer_handle = handle;
        return 0;
    }

    return -1;
}

int mk_network_service_send(int handle, const void *data, uint32_t size) {
    struct mk_network_socket_state *socket_state;
    struct mk_network_socket_state *peer;

    if (data == 0 || size == 0u || size > MK_NETWORK_SOCKET_RX_CAPACITY) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0 || !socket_state->connected || socket_state->peer_handle <= 0) {
        return -1;
    }

    peer = mk_network_socket_by_handle(socket_state->peer_handle);
    if (peer == 0 || !peer->used) {
        return -1;
    }
    if (peer->rx_size + size > MK_NETWORK_SOCKET_RX_CAPACITY) {
        return -1;
    }

    memcpy(peer->rx_buffer + peer->rx_size, data, size);
    peer->rx_size += size;
    (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_SEND,
                                   handle,
                                   socket_state->peer_handle,
                                   g_network_state.status.link_state,
                                   size);
    (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_RECV,
                                   socket_state->peer_handle,
                                   handle,
                                   g_network_state.status.link_state,
                                   peer->rx_size);
    return (int)size;
}

int mk_network_service_recv(int handle, void *buffer, uint32_t size) {
    struct mk_network_socket_state *socket_state;
    uint32_t read_size;

    if (buffer == 0 || size == 0u) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0) {
        return -1;
    }

    read_size = socket_state->rx_size;
    if (read_size > size) {
        read_size = size;
    }

    memcpy(buffer, socket_state->rx_buffer, read_size);
    if (read_size < socket_state->rx_size) {
        memmove(socket_state->rx_buffer,
                socket_state->rx_buffer + read_size,
                socket_state->rx_size - read_size);
    }
    socket_state->rx_size -= read_size;
    return (int)read_size;
}

int mk_network_service_close(int handle) {
    struct mk_network_socket_state *socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    struct mk_network_socket_state *pending;
    int peer_handle;

    if (socket_state == 0) {
        return -1;
    }

    peer_handle = socket_state->peer_handle;

    if (socket_state->pending_accept_handle > 0) {
        pending = mk_network_socket_by_handle(socket_state->pending_accept_handle);
        if (pending != 0) {
            mk_network_socket_disconnect_peer(pending);
            memset(pending, 0, sizeof(*pending));
        }
    }
    mk_network_socket_disconnect_peer(socket_state);
    (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_CLOSED,
                                   handle,
                                   peer_handle,
                                   g_network_state.status.link_state,
                                   0u);
    memset(socket_state, 0, sizeof(*socket_state));
    return 0;
}

int mk_network_service_listen(int handle, int backlog) {
    struct mk_network_socket_state *socket_state = mk_network_socket_by_handle_for_current_pid(handle);

    (void)backlog;
    if (socket_state == 0 || !socket_state->bound || socket_state->connected) {
        return -1;
    }
    socket_state->listening = 1;
    socket_state->pending_accept_handle = -1;
    return 0;
}

int mk_network_service_accept(int handle) {
    struct mk_network_socket_state *socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    int accepted_handle;

    if (socket_state == 0 || !socket_state->listening || socket_state->pending_accept_handle <= 0) {
        return -1;
    }
    accepted_handle = socket_state->pending_accept_handle;
    socket_state->pending_accept_handle = -1;
    return accepted_handle;
}

int mk_network_service_last_request(struct mk_message *message) {
    if (message == 0) {
        return -1;
    }
    if (g_last_network_request.type == MK_MSG_NONE) {
        return -1;
    }

    *message = g_last_network_request;
    return 0;
}

int mk_network_service_subscribe(struct process *subscriber) {
    struct mk_network_event_subscription *subscription;

    if (subscriber == 0) {
        return -1;
    }

    subscription = mk_network_find_subscription(subscriber);
    if (subscription == 0) {
        subscription = mk_network_alloc_subscription(subscriber);
    }
    if (subscription == 0) {
        return -1;
    }

    if (kernel_mailbox_count(&subscription->mailbox) == 0u) {
        mk_network_enqueue_event(subscription,
                                 MK_NETWORK_EVENT_STATUS,
                                 0,
                                 0,
                                 g_network_state.status.link_state,
                                 0u,
                                 ++g_network_event_sequence);
    }
    return 0;
}

int mk_network_service_event_receive(struct process *subscriber,
                                     struct mk_network_event *event,
                                     uint32_t timeout_ticks) {
    struct mk_network_event_subscription *subscription;
    uint32_t dropped_events;
    int wait_rc;

    if (subscriber == 0 || event == 0) {
        return -1;
    }

    subscription = mk_network_find_subscription(subscriber);
    if (subscription == 0) {
        return -1;
    }

    for (;;) {
        if (kernel_mailbox_try_receive(&subscription->mailbox, event) == 0) {
            return 0;
        }
        dropped_events = kernel_mailbox_dropped(&subscription->mailbox);
        if (dropped_events != 0u) {
            memset(event, 0, sizeof(*event));
            event->abi_version = 1u;
            event->event_type = MK_NETWORK_EVENT_OVERFLOW;
            event->sequence = ++g_network_event_sequence;
            event->link_state = g_network_state.status.link_state;
            event->dropped_events = dropped_events;
            kernel_mailbox_clear_dropped(&subscription->mailbox);
            event->tick = kernel_timer_get_ticks();
            return 0;
        }
        if (timeout_ticks == 0u) {
            return -1;
        }
        wait_rc = kernel_mailbox_wait(&subscription->mailbox, timeout_ticks);
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return -1;
        }
    }
}
