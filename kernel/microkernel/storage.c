#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/storage/block_device.h>
#include <kernel/kernel_string.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/service.h>
#include <kernel/microkernel/storage.h>
#include <kernel/microkernel/transfer.h>
#include <kernel/scheduler.h>
#include <kernel/userland_service.h>

static struct mk_message g_last_storage_request;
static struct mk_message g_last_storage_reply;

static uint32_t mk_storage_current_pid(void) {
    return scheduler_current_pid();
}

static int mk_storage_share_transfer(uint32_t transfer_id, uint32_t permissions) {
    const struct mk_service_record *service = mk_service_find_by_type(MK_SERVICE_STORAGE);

    if (service == 0 || service->pid <= 0) {
        return 0;
    }
    return mk_transfer_share(transfer_id, (uint32_t)service->pid, permissions);
}

static int mk_storage_local_handler(const struct mk_message *request,
                                    struct mk_message *reply,
                                    void *context) {
    (void)context;
    if (request == 0 || reply == 0) {
        return -1;
    }

    mk_message_init(reply, request->type);
    reply->source_pid = request->target_pid;
    reply->target_pid = request->source_pid;

    switch (request->type) {
    case MK_MSG_BLOCK_READ: {
        if (request->payload_size == sizeof(struct mk_storage_sectors_request)) {
            const struct mk_storage_sectors_request *payload;
            uint32_t byte_count;
            void *buffer;

            payload = (const struct mk_storage_sectors_request *)request->payload;
            byte_count = payload->sector_count * KERNEL_PERSIST_SECTOR_SIZE;
            buffer = mk_transfer_data_write(payload->transfer_id);
            if (buffer == 0 || mk_transfer_size(payload->transfer_id) < byte_count) {
                return -1;
            }
            return kernel_storage_read_sectors(payload->lba,
                                               buffer,
                                               payload->sector_count);
        }

        if (request->payload_size == sizeof(struct mk_storage_persist_request)) {
            const struct mk_storage_persist_request *payload;
            void *buffer;

            payload = (const struct mk_storage_persist_request *)request->payload;
            buffer = mk_transfer_data_write(payload->transfer_id);
            if (buffer == 0 || mk_transfer_size(payload->transfer_id) < payload->size) {
                return -1;
            }
            return kernel_storage_load(buffer, payload->size);
        }

        return -1;
    }
    case MK_MSG_BLOCK_WRITE: {
        if (request->payload_size == sizeof(struct mk_storage_sectors_request)) {
            const struct mk_storage_sectors_request *payload;
            uint32_t byte_count;
            void *buffer;

            payload = (const struct mk_storage_sectors_request *)request->payload;
            byte_count = payload->sector_count * KERNEL_PERSIST_SECTOR_SIZE;
            buffer = (void *)mk_transfer_data_read(payload->transfer_id);
            if (buffer == 0 || mk_transfer_size(payload->transfer_id) < byte_count) {
                return -1;
            }
            return kernel_storage_write_sectors(payload->lba,
                                                buffer,
                                                payload->sector_count);
        }

        if (request->payload_size == sizeof(struct mk_storage_persist_request)) {
            const struct mk_storage_persist_request *persist;
            void *buffer;

            persist = (const struct mk_storage_persist_request *)request->payload;
            buffer = (void *)mk_transfer_data_read(persist->transfer_id);
            if (buffer == 0 || mk_transfer_size(persist->transfer_id) < persist->size) {
                return -1;
            }
            return kernel_storage_save(buffer, persist->size);
        }

        return -1;
    }
    case MK_MSG_HELLO:
    case MK_MSG_BLOCK_INFO: {
        struct mk_storage_info info;

        info.total_sectors = kernel_storage_total_sectors();
        info.partition_start_lba = kernel_storage_partition_start_lba();
        return mk_message_set_payload(reply, &info, sizeof(info));
    }
    default:
        return -1;
    }
}

static int mk_storage_prepare_request(struct mk_message *message,
                                      uint32_t type,
                                      const void *payload,
                                      size_t payload_size) {
    const struct mk_service_record *service;

    service = mk_service_find_by_type(MK_SERVICE_STORAGE);
    if (service == 0) {
        return -1;
    }

    mk_message_init(message, type);
    message->source_pid = mk_storage_current_pid();
    message->target_pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    return mk_message_set_payload(message, payload, payload_size);
}

void mk_storage_service_init(void) {
    memset(&g_last_storage_request, 0, sizeof(g_last_storage_request));
    memset(&g_last_storage_reply, 0, sizeof(g_last_storage_reply));
    if (kernel_storage_ready()) {
        (void)mk_service_launch_task(MK_SERVICE_STORAGE,
                                     "storage",
                                     mk_storage_local_handler,
                                     0,
                                     userland_service_entry,
                                     8192u,
                                     MK_LAUNCH_FLAG_BOOTSTRAP |
                                     MK_LAUNCH_FLAG_BUILTIN |
                                     MK_LAUNCH_FLAG_CRITICAL);
    }
}

int mk_storage_service_ready(void) {
    return mk_service_find_by_type(MK_SERVICE_STORAGE) != 0 && kernel_storage_ready();
}

int mk_storage_service_read_sectors(uint32_t lba, void *dst, uint32_t sector_count) {
    struct mk_message request_message;
    struct mk_message reply;
    struct mk_storage_sectors_request payload;
    uint32_t transfer_id;
    uint32_t byte_count;

    if (dst == 0 || sector_count == 0u) {
        return -1;
    }
    /*
     * Storage syscalls already execute in kernel context and can safely access
     * the caller buffer through the current address space mappings. Using a
     * transfer slot here leaks heap because the kernel heap is still bump-only.
     */
    return kernel_storage_read_sectors(lba, dst, sector_count);

    byte_count = sector_count * KERNEL_PERSIST_SECTOR_SIZE;
    if (mk_transfer_create(mk_storage_current_pid(), byte_count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_storage_share_transfer(transfer_id, MK_TRANSFER_PERM_WRITE) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }

    payload.lba = lba;
    payload.sector_count = sector_count;
    payload.transfer_id = transfer_id;
    if (mk_storage_prepare_request(&request_message, MK_MSG_BLOCK_READ, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_request = request_message;
    if (mk_service_request(MK_SERVICE_STORAGE, &request_message, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_reply = reply;
    if (mk_transfer_copy_to(transfer_id, dst, byte_count) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    (void)mk_transfer_destroy(transfer_id);
    return 0;
}

int mk_storage_service_write_sectors(uint32_t lba, const void *src, uint32_t sector_count) {
    struct mk_message request_message;
    struct mk_message reply;
    struct mk_storage_sectors_request payload;
    uint32_t transfer_id;
    uint32_t byte_count;

    if (src == 0 || sector_count == 0u) {
        return -1;
    }

    byte_count = sector_count * KERNEL_PERSIST_SECTOR_SIZE;
    if (mk_transfer_create(mk_storage_current_pid(), byte_count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_storage_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, src, byte_count) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    payload.lba = lba;
    payload.sector_count = sector_count;
    payload.transfer_id = transfer_id;
    if (mk_storage_prepare_request(&request_message, MK_MSG_BLOCK_WRITE, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_request = request_message;
    if (mk_service_request(MK_SERVICE_STORAGE, &request_message, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_reply = reply;
    (void)mk_transfer_destroy(transfer_id);
    return 0;
}

int mk_storage_service_load(void *dst, uint32_t size) {
    struct mk_message request_message;
    struct mk_message reply;
    struct mk_storage_persist_request payload;
    uint32_t transfer_id;

    if (dst == 0 || size == 0u) {
        return -1;
    }
    return kernel_storage_load(dst, size);

    if (mk_transfer_create(mk_storage_current_pid(), size, &transfer_id) != 0) {
        return -1;
    }
    if (mk_storage_share_transfer(transfer_id, MK_TRANSFER_PERM_WRITE) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    payload.size = size;
    payload.transfer_id = transfer_id;
    if (mk_storage_prepare_request(&request_message, MK_MSG_BLOCK_READ, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_request = request_message;
    if (mk_service_request(MK_SERVICE_STORAGE, &request_message, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_reply = reply;
    if (mk_transfer_copy_to(transfer_id, dst, size) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    (void)mk_transfer_destroy(transfer_id);
    return 0;
}

int mk_storage_service_save(const void *src, uint32_t size) {
    struct mk_message request_message;
    struct mk_message reply;
    struct mk_storage_persist_request payload;
    uint32_t transfer_id;

    if (src == 0 || size == 0u) {
        return -1;
    }
    return kernel_storage_save(src, size);

    if (mk_transfer_create(mk_storage_current_pid(), size, &transfer_id) != 0) {
        return -1;
    }
    if (mk_storage_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, src, size) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    payload.size = size;
    payload.transfer_id = transfer_id;
    if (mk_storage_prepare_request(&request_message, MK_MSG_BLOCK_WRITE, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_request = request_message;
    if (mk_service_request(MK_SERVICE_STORAGE, &request_message, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_reply = reply;
    (void)mk_transfer_destroy(transfer_id);
    return 0;
}

uint32_t mk_storage_service_total_sectors(void) {
    struct mk_message request_message;
    struct mk_message reply;
    struct mk_storage_info info;

    if (mk_storage_prepare_request(&request_message, MK_MSG_BLOCK_INFO, 0, 0u) != 0) {
        return 0u;
    }
    g_last_storage_request = request_message;
    if (mk_service_request(MK_SERVICE_STORAGE, &request_message, &reply) != 0) {
        return 0u;
    }
    g_last_storage_reply = reply;
    if (reply.payload_size != sizeof(info)) {
        return 0u;
    }
    memcpy(&info, reply.payload, sizeof(info));
    return info.total_sectors;
}

uint32_t mk_storage_service_partition_start_lba(void) {
    struct mk_message request_message;
    struct mk_message reply;
    struct mk_storage_info info;

    if (mk_storage_prepare_request(&request_message, MK_MSG_BLOCK_INFO, 0, 0u) != 0) {
        return 0u;
    }
    g_last_storage_request = request_message;
    if (mk_service_request(MK_SERVICE_STORAGE, &request_message, &reply) != 0) {
        return 0u;
    }
    g_last_storage_reply = reply;
    if (reply.payload_size != sizeof(info)) {
        return 0u;
    }
    memcpy(&info, reply.payload, sizeof(info));
    return info.partition_start_lba;
}

int mk_storage_service_last_request(struct mk_message *message) {
    if (message == 0) {
        return -1;
    }

    *message = g_last_storage_request;
    return g_last_storage_request.type == MK_MSG_NONE ? -1 : 0;
}
