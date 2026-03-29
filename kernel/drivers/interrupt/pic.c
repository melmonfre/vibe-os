#include <stdint.h>
#include <kernel/interrupt.h>
#include <kernel/hal/io.h>

static kernel_irq_handler_t g_kernel_irq_handlers[16];

static void pic_remap(void) {
    const uint8_t mask_master = 0xFF;
    const uint8_t mask_slave = 0xFF;

    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();

    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();

    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();

    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();

    outb(0x21, mask_master);
    outb(0xA1, mask_slave);
}

static void pic_unmask_irq(uint8_t irq_line) {
    if (irq_line < 8) {
        uint8_t mask = inb(0x21);
        mask &= (uint8_t)~(1u << irq_line);
        outb(0x21, mask);
        return;
    }

    irq_line -= 8;
    uint8_t mask = inb(0xA1);
    mask &= (uint8_t)~(1u << irq_line);
    outb(0xA1, mask);
}

static void pic_mask_irq(uint8_t irq_line) {
    if (irq_line < 8) {
        uint8_t mask = inb(0x21);
        mask |= (uint8_t)(1u << irq_line);
        outb(0x21, mask);
        return;
    }

    irq_line -= 8;
    uint8_t mask = inb(0xA1);
    mask |= (uint8_t)(1u << irq_line);
    outb(0xA1, mask);
}

void kernel_pic_init(void) {
    pic_remap();
}

void kernel_irq_unmask(uint8_t irq_line) {
    if (irq_line >= 16u) {
        return;
    }
    pic_unmask_irq(irq_line);
}

void kernel_irq_mask(uint8_t irq_line) {
    if (irq_line >= 16u) {
        return;
    }
    pic_mask_irq(irq_line);
}

int kernel_irq_register_handler(uint8_t irq_line, kernel_irq_handler_t handler) {
    if (irq_line >= 16u || handler == 0) {
        return -1;
    }
    g_kernel_irq_handlers[irq_line] = handler;
    return 0;
}

void kernel_irq_dispatch(uint8_t irq_line) {
    kernel_irq_handler_t handler;

    if (irq_line >= 16u) {
        return;
    }

    handler = g_kernel_irq_handlers[irq_line];
    if (handler != 0) {
        handler();
        return;
    }

    kernel_pic_send_eoi(irq_line);
}

void kernel_irq_enable(void) {
    pic_unmask_irq(0);
    pic_unmask_irq(1);
    pic_unmask_irq(2);
    pic_unmask_irq(12);
    __asm__ volatile("sti");
}

uint32_t kernel_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void kernel_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0; popf" : : "r"(flags) : "memory", "cc");
}

void kernel_pic_send_eoi(uint8_t irq_line) {
    if (irq_line >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}
