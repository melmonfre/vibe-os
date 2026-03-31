#include <kernel/drivers/video/drm/drm.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/hal/io.h>

#define PCI_VENDOR_NVIDIA 0x10DEu

struct kernel_drm_nouveau_probe_info {
    uint16_t command;
    uint16_t subsystem_vendor;
    uint16_t subsystem_device;
    uint32_t rom_bar;
};

struct kernel_drm_nouveau_mmio_snapshot {
    uint32_t reg00;
    uint32_t reg04;
    uint32_t reg08;
    uint32_t reg0c;
};

struct kernel_drm_nouveau_vga_snapshot {
    uint8_t misc_output;
    uint8_t crtc_htotal;
    uint8_t crtc_hdisplay_end;
    uint8_t crtc_vtotal;
    uint8_t crtc_overflow;
    uint8_t crtc_vdisplay_end;
    uint8_t crtc_mode_control;
};

#define VGA_MISC_OUTPUT_READ 0x3CCu
#define VGA_CRTC_COLOR_INDEX 0x3D4u
#define VGA_CRTC_COLOR_DATA 0x3D5u
#define VGA_CRTC_MONO_INDEX 0x3B4u
#define VGA_CRTC_MONO_DATA 0x3B5u

static uint16_t kernel_drm_nouveau_vga_crtc_index_port(uint8_t misc_output) {
    return (misc_output & 0x01u) != 0u ? VGA_CRTC_COLOR_INDEX : VGA_CRTC_MONO_INDEX;
}

static uint16_t kernel_drm_nouveau_vga_crtc_data_port(uint8_t misc_output) {
    return (misc_output & 0x01u) != 0u ? VGA_CRTC_COLOR_DATA : VGA_CRTC_MONO_DATA;
}

static uint8_t kernel_drm_nouveau_vga_read_crtc(uint8_t misc_output, uint8_t index) {
    outb(kernel_drm_nouveau_vga_crtc_index_port(misc_output), index);
    return inb(kernel_drm_nouveau_vga_crtc_data_port(misc_output));
}

static uintptr_t kernel_drm_nouveau_mmio_ptr(const struct kernel_drm_candidate *candidate,
                                             uint32_t reg) {
    if (candidate == 0 || candidate->mmio_base == (uintptr_t)0) {
        return (uintptr_t)0;
    }
    if ((size_t)reg + sizeof(uint32_t) > candidate->mmio_size) {
        return (uintptr_t)0;
    }
    return candidate->mmio_base + (uintptr_t)reg;
}

static uint32_t kernel_drm_nouveau_mmio_read32(const struct kernel_drm_candidate *candidate,
                                               uint32_t reg) {
    volatile uint32_t *ptr =
        (volatile uint32_t *)(uintptr_t)kernel_drm_nouveau_mmio_ptr(candidate, reg);

    if (ptr == 0) {
        return 0u;
    }
    return *ptr;
}

static int kernel_drm_nouveau_snapshot_mmio(const struct kernel_drm_candidate *candidate,
                                            struct kernel_drm_nouveau_mmio_snapshot *snapshot_out) {
    if (candidate == 0 || snapshot_out == 0 || candidate->mmio_base == (uintptr_t)0) {
        return -1;
    }
    if (candidate->mmio_size < 0x10u) {
        return -1;
    }

    snapshot_out->reg00 = kernel_drm_nouveau_mmio_read32(candidate, 0x00u);
    snapshot_out->reg04 = kernel_drm_nouveau_mmio_read32(candidate, 0x04u);
    snapshot_out->reg08 = kernel_drm_nouveau_mmio_read32(candidate, 0x08u);
    snapshot_out->reg0c = kernel_drm_nouveau_mmio_read32(candidate, 0x0Cu);
    return 0;
}

static int kernel_drm_nouveau_snapshot_vga(struct kernel_drm_nouveau_vga_snapshot *snapshot_out) {
    uint8_t misc_output;

    if (snapshot_out == 0) {
        return -1;
    }

    misc_output = inb(VGA_MISC_OUTPUT_READ);
    snapshot_out->misc_output = misc_output;
    snapshot_out->crtc_htotal = kernel_drm_nouveau_vga_read_crtc(misc_output, 0x00u);
    snapshot_out->crtc_hdisplay_end = kernel_drm_nouveau_vga_read_crtc(misc_output, 0x01u);
    snapshot_out->crtc_vtotal = kernel_drm_nouveau_vga_read_crtc(misc_output, 0x06u);
    snapshot_out->crtc_overflow = kernel_drm_nouveau_vga_read_crtc(misc_output, 0x07u);
    snapshot_out->crtc_vdisplay_end = kernel_drm_nouveau_vga_read_crtc(misc_output, 0x12u);
    snapshot_out->crtc_mode_control = kernel_drm_nouveau_vga_read_crtc(misc_output, 0x17u);
    return 0;
}

static int kernel_drm_nouveau_read_probe_info(const struct kernel_drm_candidate *candidate,
                                              struct kernel_drm_nouveau_probe_info *info_out) {
    if (candidate == 0 || info_out == 0) {
        return -1;
    }
    if (candidate->pci.vendor_id != PCI_VENDOR_NVIDIA) {
        return -1;
    }

    info_out->command = kernel_pci_config_read_u16(candidate->pci.bus,
                                                   candidate->pci.slot,
                                                   candidate->pci.function,
                                                   0x04u);
    info_out->subsystem_vendor = kernel_pci_config_read_u16(candidate->pci.bus,
                                                            candidate->pci.slot,
                                                            candidate->pci.function,
                                                            0x2Cu);
    info_out->subsystem_device = kernel_pci_config_read_u16(candidate->pci.bus,
                                                            candidate->pci.slot,
                                                            candidate->pci.function,
                                                            0x2Eu);
    info_out->rom_bar = kernel_pci_config_read_u32(candidate->pci.bus,
                                                   candidate->pci.slot,
                                                   candidate->pci.function,
                                                   0x30u);
    return 0;
}

static int kernel_drm_nouveau_probe(const struct kernel_drm_candidate *candidate) {
    struct kernel_drm_nouveau_probe_info probe;
    struct kernel_drm_nouveau_mmio_snapshot mmio;
    struct kernel_drm_nouveau_vga_snapshot vga;
    int have_mmio_snapshot = 0;
    int have_vga_snapshot = 0;

    if (kernel_drm_nouveau_read_probe_info(candidate, &probe) != 0) {
        return -1;
    }
    if ((probe.command & PCI_COMMAND_MEMORY_SPACE) == 0u) {
        kernel_debug_printf("nouveau: memory decode disabled dev=%x rev=%x mmio=%x fb=%x\n",
                            candidate->pci.device_id,
                            candidate->pci.revision,
                            (uint32_t)candidate->mmio_base,
                            (uint32_t)candidate->fb_base);
        return -1;
    }
    if (candidate->mmio_base == (uintptr_t)0 || candidate->mmio_size == 0u) {
        kernel_debug_printf("nouveau: missing MMIO BAR dev=%x bar0=%x mmio=%x size=%x\n",
                            candidate->pci.device_id,
                            candidate->pci.bars[0],
                            (uint32_t)candidate->mmio_base,
                            (uint32_t)candidate->mmio_size);
        return -1;
    }
    if (kernel_drm_nouveau_snapshot_mmio(candidate, &mmio) == 0) {
        have_mmio_snapshot = 1;
    }
    if (kernel_drm_nouveau_snapshot_vga(&vga) == 0) {
        have_vga_snapshot = 1;
    }

    kernel_debug_printf("nouveau: probe dev=%x rev=%x class=%x subclass=%x prog_if=%x command=%x mmio=%x mmio_size=%x fb=%x fb_size=%x subsys=%x:%x irq=%x rom=%x\n",
                        candidate->pci.device_id,
                        candidate->pci.revision,
                        candidate->pci.class_code,
                        candidate->pci.subclass,
                        candidate->pci.prog_if,
                        probe.command,
                        (uint32_t)candidate->mmio_base,
                        (uint32_t)candidate->mmio_size,
                        (uint32_t)candidate->fb_base,
                        (uint32_t)candidate->fb_size,
                        probe.subsystem_vendor,
                        probe.subsystem_device,
                        candidate->pci.irq_line,
                        probe.rom_bar);
    if (have_mmio_snapshot) {
        kernel_debug_printf("nouveau: mmio reg00=%x reg04=%x reg08=%x reg0c=%x\n",
                            mmio.reg00,
                            mmio.reg04,
                            mmio.reg08,
                            mmio.reg0c);
    }
    if (have_vga_snapshot) {
        kernel_debug_printf("nouveau: vga misc=%x htotal=%x hdisp_end=%x vtotal=%x overflow=%x vdisp_end=%x mode_ctl=%x\n",
                            vga.misc_output,
                            vga.crtc_htotal,
                            vga.crtc_hdisplay_end,
                            vga.crtc_vtotal,
                            vga.crtc_overflow,
                            vga.crtc_vdisplay_end,
                            vga.crtc_mode_control);
    }
    return 0;
}

static int kernel_drm_nouveau_set_mode(const struct kernel_drm_candidate *candidate,
                                       uint32_t width,
                                       uint32_t height,
                                       uint16_t mode_id,
                                       struct video_mode *mode_out) {
    if (mode_out != 0) {
        *mode_out = (struct video_mode){0};
    }
    kernel_debug_printf("nouveau: probe-only backend, native modeset still pending dev=%x requested=%dx%d mode=%x mmio=%x fb=%x\n",
                        candidate != 0 ? candidate->pci.device_id : 0u,
                        (int)width,
                        (int)height,
                        mode_id,
                        candidate != 0 ? (uint32_t)candidate->mmio_base : 0u,
                        candidate != 0 ? (uint32_t)candidate->fb_base : 0u);
    return -1;
}

static int kernel_drm_nouveau_revert_last_modeset(void) {
    return -1;
}

static void kernel_drm_nouveau_forget_last_modeset(void) {
}

static void kernel_drm_nouveau_prepare_for_bios_modeset(void) {
}

const struct kernel_drm_backend_ops g_kernel_drm_nouveau_ops = {
    KERNEL_DRM_BACKEND_NOUVEAU,
    "native_gpu_nouveau",
    kernel_drm_nouveau_probe,
    kernel_drm_nouveau_set_mode,
    kernel_drm_nouveau_revert_last_modeset,
    kernel_drm_nouveau_forget_last_modeset,
    kernel_drm_nouveau_prepare_for_bios_modeset
};
