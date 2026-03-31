#include <kernel/drivers/video/drm/drm.h>
#include <kernel/bootinfo.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/hal/io.h>

#define GRAPHICS_BPP 8u
#define GRAPHICS_MIN_FB_ADDR 0x00100000u

#define VBE_DISPI_IOPORT_INDEX 0x01CEu
#define VBE_DISPI_IOPORT_DATA 0x01CFu
#define VBE_DISPI_INDEX_ID 0x0u
#define VBE_DISPI_INDEX_XRES 0x1u
#define VBE_DISPI_INDEX_YRES 0x2u
#define VBE_DISPI_INDEX_BPP 0x3u
#define VBE_DISPI_INDEX_ENABLE 0x4u
#define VBE_DISPI_INDEX_BANK 0x5u
#define VBE_DISPI_INDEX_VIRT_WIDTH 0x6u
#define VBE_DISPI_INDEX_X_OFFSET 0x8u
#define VBE_DISPI_INDEX_Y_OFFSET 0x9u
#define VBE_DISPI_ID0 0xB0C0u
#define VBE_DISPI_ID5 0xB0C5u
#define VBE_DISPI_ENABLED 0x01u
#define VBE_DISPI_LFB_ENABLED 0x40u
#define VBE_DISPI_NOCLEARMEM 0x80u

#define PCI_VENDOR_BOCHS_QEMU 0x1234u
#define PCI_DEVICE_BOCHS_VGA 0x1111u
#define PCI_VENDOR_VIRTUALBOX 0x80EEu
#define PCI_DEVICE_VIRTUALBOX_VGA 0xBEEFu

static int g_kernel_drm_bga_last_mode_valid = 0;
static struct bootinfo_vesa g_kernel_drm_bga_last_mode_vesa;
static uint32_t g_kernel_drm_bga_last_mode_flags = 0u;

static int kernel_drm_bga_supported(void);

static uint16_t kernel_drm_bga_read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

static void kernel_drm_bga_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

void kernel_drm_bga_prepare_for_bios_modeset(void) {
    if (!kernel_drm_bga_supported()) {
        return;
    }

    kernel_drm_bga_write(VBE_DISPI_INDEX_ENABLE, 0u);
    kernel_drm_bga_write(VBE_DISPI_INDEX_BANK, 0u);
    kernel_drm_bga_write(VBE_DISPI_INDEX_X_OFFSET, 0u);
    kernel_drm_bga_write(VBE_DISPI_INDEX_Y_OFFSET, 0u);
}

static int kernel_drm_bga_supported(void) {
    uint16_t id = kernel_drm_bga_read(VBE_DISPI_INDEX_ID);

    return id >= VBE_DISPI_ID0 && id <= VBE_DISPI_ID5;
}

static int kernel_drm_bga_snapshot_boot_mode(struct bootinfo_vesa *vesa_out,
                                             uint32_t *flags_out) {
    const volatile struct bootinfo *bootinfo =
        (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;

    if (vesa_out == 0 || flags_out == 0) {
        return -1;
    }
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return -1;
    }

    *vesa_out = bootinfo->vesa;
    *flags_out = bootinfo->flags;
    return 0;
}

static int kernel_drm_bga_boot_mode_matches(const struct bootinfo_vesa *expected_vesa,
                                            uint32_t expected_flags,
                                            const struct bootinfo_vesa *actual_vesa,
                                            uint32_t actual_flags) {
    if (expected_vesa == 0 || actual_vesa == 0) {
        return 0;
    }

    return expected_vesa->mode == actual_vesa->mode &&
           expected_vesa->fb_addr == actual_vesa->fb_addr &&
           expected_vesa->pitch == actual_vesa->pitch &&
           expected_vesa->width == actual_vesa->width &&
           expected_vesa->height == actual_vesa->height &&
           expected_vesa->bpp == actual_vesa->bpp &&
           (expected_flags & BOOTINFO_FLAG_VESA_VALID) ==
               (actual_flags & BOOTINFO_FLAG_VESA_VALID);
}

static void kernel_drm_bga_log_boot_mode_transition(const char *reason,
                                                    const struct bootinfo_vesa *expected_vesa,
                                                    uint32_t expected_flags,
                                                    const struct bootinfo_vesa *actual_vesa,
                                                    uint32_t actual_flags) {
    if (expected_vesa == 0 || actual_vesa == 0) {
        return;
    }

    kernel_debug_printf("bga: %s bootinfo expected mode=%x fb=%x pitch=%x %dx%d bpp=%d valid=%d | actual mode=%x fb=%x pitch=%x %dx%d bpp=%d valid=%d\n",
                        reason != 0 ? reason : "bootinfo",
                        expected_vesa->mode,
                        expected_vesa->fb_addr,
                        expected_vesa->pitch,
                        (int)expected_vesa->width,
                        (int)expected_vesa->height,
                        (int)expected_vesa->bpp,
                        (int)((expected_flags & BOOTINFO_FLAG_VESA_VALID) != 0u),
                        actual_vesa->mode,
                        actual_vesa->fb_addr,
                        actual_vesa->pitch,
                        (int)actual_vesa->width,
                        (int)actual_vesa->height,
                        (int)actual_vesa->bpp,
                        (int)((actual_flags & BOOTINFO_FLAG_VESA_VALID) != 0u));
}

static int kernel_drm_bga_verify_boot_mode(const char *reason,
                                           const struct bootinfo_vesa *expected_vesa,
                                           uint32_t expected_flags) {
    struct bootinfo_vesa actual_vesa;
    uint32_t actual_flags;

    if (kernel_drm_bga_snapshot_boot_mode(&actual_vesa, &actual_flags) != 0) {
        kernel_debug_printf("bga: %s bootinfo readback failed\n",
                            reason != 0 ? reason : "bootinfo");
        return -1;
    }
    if (!kernel_drm_bga_boot_mode_matches(expected_vesa,
                                          expected_flags,
                                          &actual_vesa,
                                          actual_flags)) {
        kernel_drm_bga_log_boot_mode_transition(reason,
                                                expected_vesa,
                                                expected_flags,
                                                &actual_vesa,
                                                actual_flags);
        return -1;
    }
    return 0;
}

static void kernel_drm_bga_remember_last_mode(const struct bootinfo_vesa *vesa,
                                              uint32_t flags) {
    if (vesa == 0) {
        g_kernel_drm_bga_last_mode_valid = 0;
        return;
    }

    g_kernel_drm_bga_last_mode_vesa = *vesa;
    g_kernel_drm_bga_last_mode_flags = flags;
    g_kernel_drm_bga_last_mode_valid = 1;
}

void kernel_drm_bga_forget_last_mode(void) {
    g_kernel_drm_bga_last_mode_valid = 0;
    g_kernel_drm_bga_last_mode_vesa = (struct bootinfo_vesa){0};
    g_kernel_drm_bga_last_mode_flags = 0u;
}

static int kernel_drm_bga_program_mode(uint16_t width,
                                       uint16_t height,
                                       uint8_t bpp,
                                       uint16_t *pitch_out) {
    uint16_t active_width;
    uint16_t active_height;
    uint16_t active_bpp;
    uint16_t pitch;

    if (!kernel_drm_bga_supported() || width == 0u || height == 0u || bpp != GRAPHICS_BPP) {
        return -1;
    }

    kernel_drm_bga_write(VBE_DISPI_INDEX_ENABLE, 0u);
    kernel_drm_bga_write(VBE_DISPI_INDEX_BANK, 0u);
    kernel_drm_bga_write(VBE_DISPI_INDEX_XRES, width);
    kernel_drm_bga_write(VBE_DISPI_INDEX_YRES, height);
    kernel_drm_bga_write(VBE_DISPI_INDEX_BPP, bpp);
    kernel_drm_bga_write(VBE_DISPI_INDEX_X_OFFSET, 0u);
    kernel_drm_bga_write(VBE_DISPI_INDEX_Y_OFFSET, 0u);
    kernel_drm_bga_write(VBE_DISPI_INDEX_ENABLE,
                         (uint16_t)(VBE_DISPI_ENABLED |
                                    VBE_DISPI_LFB_ENABLED |
                                    VBE_DISPI_NOCLEARMEM));

    active_width = kernel_drm_bga_read(VBE_DISPI_INDEX_XRES);
    active_height = kernel_drm_bga_read(VBE_DISPI_INDEX_YRES);
    active_bpp = kernel_drm_bga_read(VBE_DISPI_INDEX_BPP);
    pitch = kernel_drm_bga_read(VBE_DISPI_INDEX_VIRT_WIDTH);
    if (pitch < active_width) {
        pitch = active_width;
    }

    if (active_width != width ||
        active_height != height ||
        active_bpp != bpp ||
        pitch == 0u) {
        return -1;
    }

    if (pitch_out != 0) {
        *pitch_out = pitch;
    }
    return 0;
}

static int kernel_drm_bga_verify_hardware_mode(const char *reason,
                                               uint16_t expected_width,
                                               uint16_t expected_height,
                                               uint8_t expected_bpp,
                                               uint16_t expected_pitch) {
    uint16_t active_width;
    uint16_t active_height;
    uint16_t active_bpp;
    uint16_t active_pitch;

    if (!kernel_drm_bga_supported()) {
        kernel_debug_printf("bga: %s hardware readback unavailable\n",
                            reason != 0 ? reason : "hardware");
        return -1;
    }

    active_width = kernel_drm_bga_read(VBE_DISPI_INDEX_XRES);
    active_height = kernel_drm_bga_read(VBE_DISPI_INDEX_YRES);
    active_bpp = kernel_drm_bga_read(VBE_DISPI_INDEX_BPP);
    active_pitch = kernel_drm_bga_read(VBE_DISPI_INDEX_VIRT_WIDTH);
    if (active_pitch < active_width) {
        active_pitch = active_width;
    }

    if (active_width != expected_width ||
        active_height != expected_height ||
        active_bpp != expected_bpp ||
        active_pitch != expected_pitch) {
        kernel_debug_printf("bga: %s hardware expected pitch=%x %dx%d bpp=%d | actual pitch=%x %dx%d bpp=%d\n",
                            reason != 0 ? reason : "hardware",
                            expected_pitch,
                            (int)expected_width,
                            (int)expected_height,
                            (int)expected_bpp,
                            active_pitch,
                            (int)active_width,
                            (int)active_height,
                            (int)active_bpp);
        return -1;
    }
    return 0;
}

static int kernel_drm_bga_probe(const struct kernel_drm_candidate *candidate) {
    if (candidate == 0) {
        return -1;
    }

    if ((candidate->pci.vendor_id == PCI_VENDOR_BOCHS_QEMU &&
         candidate->pci.device_id == PCI_DEVICE_BOCHS_VGA) ||
        (candidate->pci.vendor_id == PCI_VENDOR_VIRTUALBOX &&
         candidate->pci.device_id == PCI_DEVICE_VIRTUALBOX_VGA)) {
        return kernel_drm_bga_supported() ? 0 : -1;
    }

    return -1;
}

static int kernel_drm_bga_set_mode(const struct kernel_drm_candidate *candidate,
                                   uint32_t width,
                                   uint32_t height,
                                   uint16_t mode_id,
                                   struct video_mode *mode_out) {
    volatile struct bootinfo *bootinfo;
    struct bootinfo_vesa previous_vesa;
    uint32_t previous_flags;
    uint32_t fb_addr;
    uint16_t pitch;
    struct bootinfo_vesa expected_vesa;
    uint32_t expected_flags;

    (void)candidate;

    if (mode_out == 0 || width > 0xFFFFu || height > 0xFFFFu || !kernel_drm_bga_supported()) {
        return -1;
    }

    bootinfo = (volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return -1;
    }

    fb_addr = bootinfo->vesa.fb_addr;
    if (fb_addr < GRAPHICS_MIN_FB_ADDR) {
        return -1;
    }

    kernel_drm_bga_forget_last_mode();
    if (kernel_drm_bga_snapshot_boot_mode(&previous_vesa, &previous_flags) != 0) {
        return -1;
    }
    if (kernel_drm_bga_program_mode((uint16_t)width,
                                    (uint16_t)height,
                                    GRAPHICS_BPP,
                                    &pitch) != 0) {
        return -1;
    }

    bootinfo->flags |= BOOTINFO_FLAG_VESA_VALID;
    bootinfo->vesa.mode = mode_id;
    bootinfo->vesa.fb_addr = fb_addr;
    bootinfo->vesa.pitch = pitch;
    bootinfo->vesa.width = (uint16_t)width;
    bootinfo->vesa.height = (uint16_t)height;
    bootinfo->vesa.bpp = GRAPHICS_BPP;
    expected_vesa = bootinfo->vesa;
    expected_flags = bootinfo->flags;

    if (kernel_drm_bga_verify_hardware_mode("set-mode",
                                            (uint16_t)width,
                                            (uint16_t)height,
                                            GRAPHICS_BPP,
                                            pitch) != 0 ||
        kernel_drm_bga_verify_boot_mode("set-mode", &expected_vesa, expected_flags) != 0) {
        return -1;
    }

    mode_out->fb_addr = fb_addr;
    mode_out->pitch = pitch;
    mode_out->width = width;
    mode_out->height = height;
    mode_out->bpp = GRAPHICS_BPP;
    kernel_drm_bga_remember_last_mode(&previous_vesa, previous_flags);
    return 0;
}

int kernel_drm_bga_revert_last_mode(void) {
    volatile struct bootinfo *bootinfo;
    struct bootinfo_vesa previous_vesa;
    uint32_t previous_flags;
    uint16_t pitch;
    struct bootinfo_vesa expected_vesa;
    uint32_t expected_flags;

    if (!g_kernel_drm_bga_last_mode_valid) {
        return -1;
    }

    previous_vesa = g_kernel_drm_bga_last_mode_vesa;
    previous_flags = g_kernel_drm_bga_last_mode_flags;
    kernel_drm_bga_forget_last_mode();

    if ((previous_flags & BOOTINFO_FLAG_VESA_VALID) == 0u ||
        previous_vesa.fb_addr < GRAPHICS_MIN_FB_ADDR ||
        previous_vesa.width == 0u ||
        previous_vesa.height == 0u ||
        previous_vesa.bpp != GRAPHICS_BPP) {
        return -1;
    }
    if (kernel_drm_bga_program_mode(previous_vesa.width,
                                    previous_vesa.height,
                                    previous_vesa.bpp,
                                    &pitch) != 0) {
        return -1;
    }

    bootinfo = (volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    if (bootinfo->magic != BOOTINFO_MAGIC || bootinfo->version != BOOTINFO_VERSION) {
        return -1;
    }

    bootinfo->vesa = previous_vesa;
    bootinfo->vesa.pitch = pitch;
    bootinfo->flags = (bootinfo->flags & ~BOOTINFO_FLAG_VESA_VALID) |
                      (previous_flags & BOOTINFO_FLAG_VESA_VALID);
    expected_vesa = bootinfo->vesa;
    expected_flags = bootinfo->flags;
    if (kernel_drm_bga_verify_hardware_mode("revert",
                                            previous_vesa.width,
                                            previous_vesa.height,
                                            previous_vesa.bpp,
                                            pitch) != 0 ||
        kernel_drm_bga_verify_boot_mode("revert", &expected_vesa, expected_flags) != 0) {
        return -1;
    }
    return 0;
}

const struct kernel_drm_backend_ops g_kernel_drm_bga_ops = {
    KERNEL_DRM_BACKEND_BGA,
    "native_gpu_bga",
    kernel_drm_bga_probe,
    kernel_drm_bga_set_mode,
    kernel_drm_bga_revert_last_mode,
    kernel_drm_bga_forget_last_mode,
    kernel_drm_bga_prepare_for_bios_modeset
};
