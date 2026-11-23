#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/pool.h>
#include <sys/queue.h>
#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>
#include <dev/usb/uhcireg.h>
#include <dev/usb/uhcivar.h>

#include <kernel/pci_bus.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/interrupt.h>
#include <kernel/memory/heap.h>

#define MAX_BSD_USB_CONTROLLERS 8

struct bsd_usb_controller {
    int type;
    pci_device_t *pdev;
    void *softc;
    int active;
    uint8_t irq;
};

static struct bsd_usb_controller g_bsd_controllers[MAX_BSD_USB_CONTROLLERS];
static int g_bsd_controller_count = 0;
static int g_bsd_initialized = 0;

extern int uhci_intr(void *);
extern usbd_status uhci_init(struct uhci_softc *);
extern usbd_status uhci_run(struct uhci_softc *, int);

/* OpenBSD dev/usb/xhci.c (vendorizado em compat/sys): controller xHCI real.
 * O driver fala so com MMIO e IRQ (sem PCI MSI-X neste caminho) entao o
 * attach e direto, igual ao do UHCI. */
#include <sys/rwlock.h>
#include <dev/usb/xhcireg.h>
#include <dev/usb/xhcivar.h>

extern int xhci_init(struct xhci_softc *);
extern void xhci_config(struct xhci_softc *);
extern int xhci_intr(void *);

static struct xhci_softc *g_xhci_sc = NULL;

static void bsd_xhci_irq(void) {
    if (g_xhci_sc != NULL) {
        xhci_intr(g_xhci_sc);
    }
}

extern struct pool *uhcixfer;

static int bsd_usb_attach_uhci(pci_device_t *pdev) {
    struct uhci_softc *sc;
    bus_space_handle_t ioh;
    int bar_idx = -1;
    uint8_t irq = pdev->irq_line;

    sc = kernel_malloc(sizeof(struct uhci_softc));
    if (!sc) {
        kernel_debug_printf("bsd_usb: OOM for uhci_softc\n");
        return -1;
    }

    for (int b = 0; b < 6; b++) {
        if (pdev->bar_type[b] == PCI_BAR_TYPE_IO && pdev->bar_size[b] > 0) {
            bar_idx = b;
            break;
        }
    }
    if (bar_idx < 0) {
        kernel_debug_printf("bsd_usb: UHCI controller has no IO BAR\n");
        kernel_free(sc);
        return -1;
    }

    ioh = (bus_space_handle_t)(uintptr_t)pdev->bar_addr[bar_idx];
    sc->iot = 0;
    sc->ioh = ioh;
    sc->sc_size = (bus_size_t)pdev->bar_size[bar_idx];
    sc->sc_bus.dmatag = NULL;
    sc->sc_bus.use_polling = 1;
    sc->sc_bus.dying = 0;
    sc->sc_bus.pipe_size = sizeof(struct usbd_pipe);
    sc->sc_bus.usbrev = USBREV_1_1;
    sc->sc_bus.methods = NULL;
    sc->sc_bus.intr_context = 0;
    sc->sc_bus.no_intrs = 0;
    sc->sc_id_vendor = pdev->vendor_id;

    sc->sc_bus.bdev.dv_xname = "uhci";

    struct bsd_usb_controller *ctrl = &g_bsd_controllers[g_bsd_controller_count];
    ctrl->type = 1;
    ctrl->pdev = pdev;
    ctrl->softc = sc;
    ctrl->active = 0;  /* deferred - not yet initialized */
    g_bsd_controller_count++;

    kernel_debug_printf("bsd_usb: UHCI at %02x:%02x.%x IO=%x IRQ=%d (deferred)\n",
        pdev->bus, pdev->slot, pdev->function,
        (unsigned int)pdev->bar_addr[bar_idx], irq);
    return 0;
}

static int bsd_usb_attach_xhci(pci_device_t *pdev) {
    struct xhci_softc *sc;
    bus_space_handle_t ioh;
    int bar_idx = -1;
    uint8_t irq = pdev->irq_line;

    sc = kernel_malloc(sizeof(struct xhci_softc));
    if (!sc) {
        kernel_debug_printf("bsd_usb: OOM for xhci_softc\n");
        return -1;
    }

    /* O medidor de tamanho de BAR do kernel devolve 0 quando o barramento
     * nao deixa progredir o size-probe (medido com qemu-xhci no q35: bar=0
     * mmio=febd4000 size=0). A base e o dado que importa ao driver, entao o
     * filtro e por tipo + base != 0; o tamanho so e repassado quando medido. */
    for (int b = 0; b < 6; b++) {
        if ((pdev->bar_type[b] == PCI_BAR_TYPE_MEM32 ||
             pdev->bar_type[b] == PCI_BAR_TYPE_MEM64) &&
            pdev->bar_addr[b] != 0u) {
            bar_idx = b;
            break;
        }
    }
    if (bar_idx < 0) {
        kernel_debug_printf("bsd_usb: xHCI controller has no MMIO BAR\n");
        kernel_free(sc);
        return -1;
    }

    ioh = (bus_space_handle_t)(uintptr_t)pdev->bar_addr[bar_idx];

    /* A identificacao do controlador ja veio do class/subclass/prog_if do PCI
     * (0x0C/0x03/prog_if != 0x00); o driver xHCI le CAPLENGTH/oper/runt/door
     * por conta propria em xhci_init. Um read de 4 bytes em offset 0 aqui
     * misturaria CAPLENGTH (byte 0) com HCIVERSION (bytes 2-3) e deu falso
     * negativo (medido: 0x40 em qemu-xhci), entao nao se repete o probe. */

    sc->iot = 0;
    sc->ioh = ioh;
    sc->sc_size = (bus_size_t)pdev->bar_size[bar_idx];
    sc->sc_bus.dmatag = NULL;
    sc->sc_bus.use_polling = 1;
    sc->sc_bus.dying = 0;
    sc->sc_bus.intr_context = 0;
    sc->sc_bus.no_intrs = 0;
    sc->sc_id_vendor = pdev->vendor_id;
    sc->sc_noslot = USB_MAX_DEVICES;
    sc->sc_bus.bdev.dv_xname = "xhci";

    struct bsd_usb_controller *ctrl = &g_bsd_controllers[g_bsd_controller_count];
    ctrl->type = 2;
    ctrl->pdev = pdev;
    ctrl->softc = sc;
    ctrl->active = 0;
    ctrl->irq = irq;
    g_bsd_controller_count++;

    kernel_debug_printf("bsd_usb: xHCI at %02x:%02x.%x MMIO=%x IRQ=%d (deferred)\n",
        pdev->bus, pdev->slot, pdev->function,
        (unsigned int)pdev->bar_addr[bar_idx], irq);
    return 0;
}

int bsd_usb_init_controller(struct bsd_usb_controller *ctrl) {
    if (!ctrl || !ctrl->softc || ctrl->active) return -1;

    if (ctrl->type == 2) {
        struct xhci_softc *xsc = (struct xhci_softc *)ctrl->softc;
        int rv = xhci_init(xsc);
        if (rv != 0) {
            kernel_debug_printf("bsd_usb: xHCI hw init failed: %d\n", rv);
            return -1;
        }
        xhci_config(xsc);
        g_xhci_sc = xsc;
        if (ctrl->irq != 0) {
            kernel_irq_register_handler(ctrl->irq, bsd_xhci_irq);
            kernel_irq_unmask(ctrl->irq);
        }
        ctrl->active = 1;
        kernel_debug_printf("bsd_usb: xHCI hw init OK (ports=%d)\n", xsc->sc_noport);
        return 0;
    }

    struct uhci_softc *sc = (struct uhci_softc *)ctrl->softc;

    int s = splhardusb();
    bus_space_write_2(sc->iot, sc->ioh, UHCI_INTR, 0);
    uhci_run(sc, 0);
    bus_space_write_2(sc->iot, sc->ioh, UHCI_INTR, 0);
    usbd_status r = uhci_init(sc);
    splx(s);

    if (r == USBD_NORMAL_COMPLETION) {
        ctrl->active = 1;
        kernel_debug_printf("bsd_usb: UHCI hw init OK\n");
        return 0;
    }
    kernel_debug_printf("bsd_usb: UHCI hw init failed: %d\n", r);
    return -1;
}

/* Bring-up sincrono do controlador attachado: roda o init real do driver
 * (xhci_init faz o reset + rings + event ring; uhci_init faz o mesmo no
 * UHCI) e arma a IRQ. Chamado pelo usb_host logo apos o attach, porque
 * bsd_usb_init apenas REGISTRA os controladores — sem esta chamada eles
 * ficam "deferred" para sempre e nenhum device e enumerado. */
int bsd_usb_init_attached(void) {
    int ok = 0;
    for (int i = 0; i < g_bsd_controller_count; i++) {
        struct bsd_usb_controller *ctrl = &g_bsd_controllers[i];
        if (ctrl->active) { ok++; continue; }
        if (bsd_usb_init_controller(ctrl) == 0) {
            ok++;
        } else {
            kernel_debug_printf("bsd_usb: controller %d (type=%d) init failed\n", i, ctrl->type);
        }
    }
    kernel_debug_printf("bsd_usb: %d/%d controller(s) active\n", ok, g_bsd_controller_count);
    return ok;
}

void bsd_usb_init(void) {
    if (g_bsd_initialized) return;
    g_bsd_initialized = 1;

    if (!kernel_heap_is_initialized()) {
        kernel_debug_printf("bsd_usb: heap not ready, deferring\n");
        g_bsd_initialized = 0;
        return;
    }

    kernel_debug_printf("bsd_usb: probing for UHCI controllers...\n");

    int total = pci_device_count();
    if (total <= 0) {
        pci_bus_init();
        total = pci_device_count();
    }

    for (int i = 0; i < total && g_bsd_controller_count < MAX_BSD_USB_CONTROLLERS; i++) {
        pci_device_t *pdev = pci_device_get(i);
        if (!pdev) continue;
        if (pdev->class_code == 0x0C && pdev->subclass == 0x03) {
            if (pdev->prog_if == 0x00) {
                bsd_usb_attach_uhci(pdev);
            } else {
                bsd_usb_attach_xhci(pdev);
            }
        }
    }

    if (g_bsd_controller_count == 0) {
        kernel_debug_printf("bsd_usb: no UHCI controllers found\n");
    } else {
        kernel_debug_printf("bsd_usb: %d UHCI controller(s) initialized\n",
            g_bsd_controller_count);
    }
}
