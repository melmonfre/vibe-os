#include <kernel/drivers/video/drm/drm.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/video/drm/i915/i915.h>

#define PCI_VENDOR_BOCHS_QEMU 0x1234u
#define PCI_DEVICE_BOCHS_VGA 0x1111u
#define PCI_VENDOR_VIRTUALBOX 0x80EEu
#define PCI_DEVICE_VIRTUALBOX_VGA 0xBEEFu
#define PCI_VENDOR_REDHAT_QUMRANET 0x1B36u
#define PCI_DEVICE_QXL_VGA 0x0100u
#define PCI_VENDOR_VMWARE 0x15ADu
#define PCI_DEVICE_VMWARE_SVGA2 0x0405u
#define PCI_VENDOR_CIRRUS 0x1013u
#define PCI_DEVICE_CIRRUS_5446 0x00B8u
#define PCI_VENDOR_VIRTIO 0x1AF4u
#define PCI_DEVICE_VIRTIO_VGA 0x1050u
#define PCI_VENDOR_INTEL 0x8086u
#define PCI_VENDOR_AMD 0x1002u
#define PCI_VENDOR_NVIDIA 0x10DEu

static const struct kernel_drm_backend_ops *g_kernel_drm_backends[] = {
    &g_kernel_drm_bga_ops,
    &g_kernel_drm_i915_ops,
    &g_kernel_drm_radeon_ops,
    &g_kernel_drm_nouveau_ops,
};
static enum kernel_drm_backend_kind g_kernel_drm_last_modeset_backend =
    KERNEL_DRM_BACKEND_NONE;
static enum kernel_drm_backend_kind g_kernel_drm_last_attempted_backend =
    KERNEL_DRM_BACKEND_NONE;
static enum kernel_drm_backend_kind g_kernel_drm_active_backend =
    KERNEL_DRM_BACKEND_NONE;
static enum kernel_drm_backend_kind g_kernel_drm_detected_backend =
    KERNEL_DRM_BACKEND_NONE;
static struct kernel_pci_device_info g_kernel_drm_active_pci = {0};
static struct kernel_pci_device_info g_kernel_drm_detected_pci = {0};

static void kernel_drm_clear_pci_info(struct kernel_pci_device_info *info) {
    if (info == 0) {
        return;
    }
    *info = (struct kernel_pci_device_info){0};
}

static int kernel_drm_candidate_kind(const struct kernel_pci_device_info *info,
                                     enum kernel_drm_backend_kind *kind_out,
                                     const char **name_out,
                                     const char **device_name_out) {
    if ((info->vendor_id == PCI_VENDOR_BOCHS_QEMU &&
         info->device_id == PCI_DEVICE_BOCHS_VGA) ||
        (info->vendor_id == PCI_VENDOR_VIRTUALBOX &&
         info->device_id == PCI_DEVICE_VIRTUALBOX_VGA)) {
        *kind_out = KERNEL_DRM_BACKEND_BGA;
        *name_out = "native_gpu_bga";
        *device_name_out = "bochs_vbe";
        return 0;
    }
    if (info->vendor_id == PCI_VENDOR_REDHAT_QUMRANET &&
        info->device_id == PCI_DEVICE_QXL_VGA) {
        *kind_out = KERNEL_DRM_BACKEND_UNKNOWN;
        *name_out = "native_gpu_qxl";
        *device_name_out = "qxl_vga";
        return 0;
    }
    if (info->vendor_id == PCI_VENDOR_VMWARE &&
        info->device_id == PCI_DEVICE_VMWARE_SVGA2) {
        *kind_out = KERNEL_DRM_BACKEND_UNKNOWN;
        *name_out = "native_gpu_vmware";
        *device_name_out = "vmware_svga2";
        return 0;
    }
    if (info->vendor_id == PCI_VENDOR_CIRRUS &&
        info->device_id == PCI_DEVICE_CIRRUS_5446) {
        *kind_out = KERNEL_DRM_BACKEND_UNKNOWN;
        *name_out = "native_gpu_cirrus";
        *device_name_out = "cirrus_5446";
        return 0;
    }
    if (info->vendor_id == PCI_VENDOR_VIRTIO &&
        info->device_id == PCI_DEVICE_VIRTIO_VGA) {
        *kind_out = KERNEL_DRM_BACKEND_UNKNOWN;
        *name_out = "native_gpu_virtio";
        *device_name_out = "virtio_vga";
        return 0;
    }
    if (info->vendor_id == PCI_VENDOR_INTEL) {
        if (kernel_drm_i915_is_modeset_supported_device(info->device_id)) {
            *kind_out = KERNEL_DRM_BACKEND_I915;
            *name_out = "native_gpu_i915";
            *device_name_out = "intel_display";
        } else if (kernel_drm_i915_is_supported_device(info->device_id)) {
            *kind_out = KERNEL_DRM_BACKEND_UNKNOWN;
            *name_out = "native_gpu_intel_legacy";
            *device_name_out = "intel_display_legacy";
        } else {
            *kind_out = KERNEL_DRM_BACKEND_UNKNOWN;
            *name_out = "native_gpu_intel_unsupported";
            *device_name_out = "intel_display_unsupported";
        }
        return 0;
    }
    if (info->vendor_id == PCI_VENDOR_AMD) {
        *kind_out = KERNEL_DRM_BACKEND_RADEON;
        *name_out = "native_gpu_radeon";
        *device_name_out = "amd_display";
        return 0;
    }
    if (info->vendor_id == PCI_VENDOR_NVIDIA) {
        *kind_out = KERNEL_DRM_BACKEND_NOUVEAU;
        *name_out = "native_gpu_nouveau";
        *device_name_out = "nvidia_display";
        return 0;
    }
    *kind_out = KERNEL_DRM_BACKEND_UNKNOWN;
    *name_out = "native_gpu_unknown";
    *device_name_out = "unknown_display";
    return 0;
}

struct kernel_drm_probe_ctx {
    struct kernel_drm_candidate *out;
    size_t capacity;
    size_t count;
};

static const struct kernel_drm_backend_ops *kernel_drm_find_backend(enum kernel_drm_backend_kind kind) {
    size_t i;

    for (i = 0u; i < (sizeof(g_kernel_drm_backends) / sizeof(g_kernel_drm_backends[0])); ++i) {
        if (g_kernel_drm_backends[i] != 0 && g_kernel_drm_backends[i]->kind == kind) {
            return g_kernel_drm_backends[i];
        }
    }
    return 0;
}

static const char *kernel_drm_backend_label(enum kernel_drm_backend_kind kind) {
    const struct kernel_drm_backend_ops *ops = kernel_drm_find_backend(kind);

    if (ops != 0 && ops->name != 0) {
        return ops->name;
    }

    switch (kind) {
    case KERNEL_DRM_BACKEND_BGA:
        return "native_gpu_bga";
    case KERNEL_DRM_BACKEND_I915:
        return "native_gpu_i915";
    case KERNEL_DRM_BACKEND_RADEON:
        return "native_gpu_radeon";
    case KERNEL_DRM_BACKEND_NOUVEAU:
        return "native_gpu_nouveau";
    case KERNEL_DRM_BACKEND_UNKNOWN:
        return "native_gpu_unknown";
    default:
        return "none";
    }
}

static void kernel_drm_prepare_backend_for_bios(enum kernel_drm_backend_kind kind) {
    const struct kernel_drm_backend_ops *ops;

    if (kind == KERNEL_DRM_BACKEND_NONE) {
        return;
    }

    ops = kernel_drm_find_backend(kind);
    if (ops == 0 || ops->prepare_for_bios_modeset == 0) {
        return;
    }

    kernel_debug_printf("drm: preparing backend=%s for BIOS modeset\n",
                        kernel_drm_backend_label(kind));
    ops->prepare_for_bios_modeset();
}

static void kernel_drm_clear_mode_out(struct video_mode *mode_out) {
    if (mode_out == 0) {
        return;
    }
    *mode_out = (struct video_mode){0};
}

static int kernel_drm_probe_cb(const struct kernel_pci_device_info *info, void *ctx_ptr) {
    struct kernel_drm_probe_ctx *ctx = (struct kernel_drm_probe_ctx *)ctx_ptr;
    struct kernel_drm_candidate candidate;

    if (info->class_code != PCI_CLASS_DISPLAY) {
        return 0;
    }
    if (info->subclass != PCI_SUBCLASS_DISPLAY_VGA &&
        info->subclass != PCI_SUBCLASS_DISPLAY_XGA &&
        info->subclass != PCI_SUBCLASS_DISPLAY_3D) {
        return 0;
    }

    (void)kernel_drm_candidate_kind(info,
                                    &candidate.backend_kind,
                                    &candidate.backend_name,
                                    &candidate.device_name);
    candidate.pci = *info;
    candidate.mmio_base = kernel_pci_bar_base(info->bars[0]);
    candidate.mmio_size = kernel_pci_bar_size(info->bus, info->slot, info->function, 0u);
    candidate.fb_base = kernel_pci_bar_base(info->bars[1]);
    candidate.fb_size = kernel_pci_bar_size(info->bus, info->slot, info->function, 1u);
    if (candidate.backend_kind == KERNEL_DRM_BACKEND_I915) {
        candidate.fb_base = kernel_pci_bar_base(info->bars[2]);
        candidate.fb_size = kernel_pci_bar_size(info->bus, info->slot, info->function, 2u);
    }

    if (ctx->count < ctx->capacity) {
        ctx->out[ctx->count] = candidate;
    }
    ctx->count += 1u;
    return 0;
}

size_t kernel_drm_probe_candidates(struct kernel_drm_candidate *out, size_t capacity) {
    struct kernel_drm_probe_ctx ctx;

    ctx.out = out;
    ctx.capacity = out != 0 ? capacity : 0u;
    ctx.count = 0u;
    kernel_pci_enumerate(kernel_drm_probe_cb, &ctx);
    return ctx.count;
}

void kernel_drm_log_candidates(void) {
    struct kernel_drm_candidate candidates[8];
    size_t count = kernel_drm_probe_candidates(candidates, 8u);
    size_t i;

    g_kernel_drm_detected_backend = KERNEL_DRM_BACKEND_NONE;
    kernel_drm_clear_pci_info(&g_kernel_drm_detected_pci);
    if (count == 0u) {
        kernel_debug_puts("drm: no native candidate detected on PCI display class\n");
        return;
    }

    if (count > 8u) {
        count = 8u;
    }

    for (i = 0u; i < count; ++i) {
        const struct kernel_drm_backend_ops *ops = kernel_drm_find_backend(candidates[i].backend_kind);
        int probe_rc = -1;

        if (candidates[i].backend_kind != KERNEL_DRM_BACKEND_UNKNOWN &&
            ops != 0 &&
            ops->probe != 0) {
            probe_rc = ops->probe(&candidates[i]);
        }

        kernel_debug_printf("drm: candidate backend=%s device=%s vendor=%x device=%x class=%x subclass=%x bus=%d slot=%d fn=%d mmio=%x mmio_size=%x fb=%x fb_size=%x probe=%d\n",
                            candidates[i].backend_name,
                            candidates[i].device_name != 0 ? candidates[i].device_name : "unknown",
                            candidates[i].pci.vendor_id,
                            candidates[i].pci.device_id,
                            candidates[i].pci.class_code,
                            candidates[i].pci.subclass,
                            (int)candidates[i].pci.bus,
                            (int)candidates[i].pci.slot,
                            (int)candidates[i].pci.function,
                            (uint32_t)candidates[i].mmio_base,
                            (uint32_t)candidates[i].mmio_size,
                            (uint32_t)candidates[i].fb_base,
                            (uint32_t)candidates[i].fb_size,
                            probe_rc);

        if (g_kernel_drm_detected_backend == KERNEL_DRM_BACKEND_NONE &&
            candidates[i].backend_kind != KERNEL_DRM_BACKEND_UNKNOWN &&
            probe_rc == 0) {
            g_kernel_drm_detected_backend = candidates[i].backend_kind;
            g_kernel_drm_detected_pci = candidates[i].pci;
        }
    }
}

int kernel_drm_try_set_mode(uint32_t width,
                            uint32_t height,
                            uint16_t mode_id,
                            struct video_mode *mode_out) {
    struct kernel_drm_candidate candidates[8];
    size_t count = kernel_drm_probe_candidates(candidates, 8u);
    size_t i;

    kernel_drm_forget_last_modeset();
    g_kernel_drm_last_attempted_backend = KERNEL_DRM_BACKEND_NONE;
    kernel_drm_clear_mode_out(mode_out);

    for (i = 0u; i < count && i < 8u; ++i) {
        const struct kernel_drm_backend_ops *ops = kernel_drm_find_backend(candidates[i].backend_kind);

        if (ops == 0 || ops->probe == 0 || ops->set_mode == 0) {
            continue;
        }
        if (ops->probe(&candidates[i]) != 0) {
            continue;
        }
        if (g_kernel_drm_detected_backend == KERNEL_DRM_BACKEND_NONE) {
            g_kernel_drm_detected_backend = candidates[i].backend_kind;
            g_kernel_drm_detected_pci = candidates[i].pci;
        }
        g_kernel_drm_last_attempted_backend = candidates[i].backend_kind;
        if (ops->set_mode(&candidates[i], width, height, mode_id, mode_out) == 0) {
            g_kernel_drm_last_modeset_backend = candidates[i].backend_kind;
            g_kernel_drm_last_attempted_backend = KERNEL_DRM_BACKEND_NONE;
            g_kernel_drm_active_backend = candidates[i].backend_kind;
            g_kernel_drm_active_pci = candidates[i].pci;
            kernel_debug_printf("drm: modeset backend=%s ",
                                ops->name != 0 ? ops->name : candidates[i].backend_name);
            kernel_debug_printf("%d", (int)width);
            kernel_debug_putc('x');
            kernel_debug_printf("%d", (int)height);
            kernel_debug_puts(" ok\n");
            return 0;
        }
        kernel_drm_clear_mode_out(mode_out);
        kernel_debug_printf("drm: modeset backend=%s ",
                            ops->name != 0 ? ops->name : candidates[i].backend_name);
        kernel_debug_printf("%d", (int)width);
        kernel_debug_putc('x');
        kernel_debug_printf("%d", (int)height);
        kernel_debug_puts(" rejected\n");
    }

    return -1;
}

enum kernel_drm_backend_kind kernel_drm_active_backend_kind(void) {
    return g_kernel_drm_active_backend;
}

const char *kernel_drm_active_backend_name(void) {
    return kernel_drm_backend_label(g_kernel_drm_active_backend);
}

enum kernel_drm_backend_kind kernel_drm_detected_backend_kind(void) {
    return g_kernel_drm_detected_backend;
}

const char *kernel_drm_detected_backend_name(void) {
    return kernel_drm_backend_label(g_kernel_drm_detected_backend);
}

const struct kernel_pci_device_info *kernel_drm_active_pci_info(void) {
    if (g_kernel_drm_active_backend == KERNEL_DRM_BACKEND_NONE ||
        g_kernel_drm_active_pci.vendor_id == 0u) {
        return 0;
    }
    return &g_kernel_drm_active_pci;
}

const struct kernel_pci_device_info *kernel_drm_detected_pci_info(void) {
    if (g_kernel_drm_detected_backend == KERNEL_DRM_BACKEND_NONE ||
        g_kernel_drm_detected_pci.vendor_id == 0u) {
        return 0;
    }
    return &g_kernel_drm_detected_pci;
}

int kernel_drm_revert_last_modeset(void) {
    enum kernel_drm_backend_kind backend = g_kernel_drm_last_modeset_backend;
    const struct kernel_drm_backend_ops *ops = kernel_drm_find_backend(backend);

    g_kernel_drm_last_modeset_backend = KERNEL_DRM_BACKEND_NONE;
    g_kernel_drm_last_attempted_backend = KERNEL_DRM_BACKEND_NONE;
    if (backend == KERNEL_DRM_BACKEND_NONE) {
        kernel_debug_puts("drm: revert requested without a recorded native modeset\n");
        return -1;
    }
    if (ops == 0 || ops->revert_last_modeset == 0) {
        kernel_debug_printf("drm: revert not implemented for backend=%s\n",
                            kernel_drm_backend_label(backend));
        return -1;
    }

    if (ops->revert_last_modeset() == 0) {
        g_kernel_drm_active_backend = KERNEL_DRM_BACKEND_NONE;
        kernel_drm_clear_pci_info(&g_kernel_drm_active_pci);
        return 0;
    }
    return -1;
}

void kernel_drm_forget_last_modeset(void) {
    const struct kernel_drm_backend_ops *ops = kernel_drm_find_backend(g_kernel_drm_last_modeset_backend);

    if (ops != 0 && ops->forget_last_modeset != 0) {
        ops->forget_last_modeset();
    }
    g_kernel_drm_last_modeset_backend = KERNEL_DRM_BACKEND_NONE;
    g_kernel_drm_last_attempted_backend = KERNEL_DRM_BACKEND_NONE;
    kernel_drm_clear_pci_info(&g_kernel_drm_active_pci);
}

void kernel_drm_prepare_for_bios_modeset(void) {
    enum kernel_drm_backend_kind backend = g_kernel_drm_last_modeset_backend;

    if (backend == KERNEL_DRM_BACKEND_NONE) {
        backend = g_kernel_drm_last_attempted_backend;
    }
    g_kernel_drm_active_backend = KERNEL_DRM_BACKEND_NONE;
    kernel_drm_clear_pci_info(&g_kernel_drm_active_pci);
    kernel_drm_prepare_backend_for_bios(backend);
    if (backend == g_kernel_drm_last_attempted_backend) {
        g_kernel_drm_last_attempted_backend = KERNEL_DRM_BACKEND_NONE;
    }
}
