/*
 * pcie_dma.c — QEMU PCIe DMA device model
 *
 * Emulates a simple PCIe DMA engine:
 *   BAR0 (MMIO, 4KB): control/status registers
 *   BAR1 (MMIO, 1MB): device internal buffer
 *
 * Register map (BAR0):
 *   0x00  DMA_SRC       host physical address (lo 32 bits)
 *   0x04  DMA_SRC_HI    host physical address (hi 32 bits)
 *   0x08  DMA_DST       device buffer offset
 *   0x0C  DMA_LEN       transfer length in bytes (max 1MB)
 *   0x10  DMA_CMD       write 1 = H2D transfer, write 2 = D2H transfer
 *   0x14  DMA_STATUS    0=idle, 1=busy, 2=done, 3=error
 *   0x18  DMA_IRQ_MASK  1 = enable completion interrupt
 *   0x1C  DMA_IRQ_ACK   write 1 to clear interrupt
 *
 * Vendor:Device = 0x1234:0xDEAD
 *
 * Build: copy this file into qemu/hw/misc/pcie_dma.c
 *        add to qemu/hw/misc/meson.build:
 *          system_ss.add(files('pcie_dma.c'))
 *        rebuild QEMU with:
 *          ./configure --target-list=aarch64-softmmu && make -j$(nproc)
 *
 * Launch flag: -device pcie-dma
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "sysemu/dma.h"
#include "hw/pci/msi.h"

#define TYPE_PCIE_DMA "pcie-dma"
OBJECT_DECLARE_SIMPLE_TYPE(PCIeDMAState, PCIE_DMA)

/* Register offsets within BAR0 */
#define REG_DMA_SRC         0x00
#define REG_DMA_SRC_HI      0x04
#define REG_DMA_DST         0x08
#define REG_DMA_LEN         0x0C
#define REG_DMA_CMD         0x10
#define REG_DMA_STATUS      0x14
#define REG_DMA_IRQ_MASK    0x18
#define REG_DMA_IRQ_ACK     0x1C

/* DMA_CMD values */
#define CMD_H2D             0x01    /* host -> device */
#define CMD_D2H             0x02    /* device -> host */

/* DMA_STATUS values */
#define STATUS_IDLE         0x00
#define STATUS_BUSY         0x01
#define STATUS_DONE         0x02
#define STATUS_ERROR        0x03

#define DMA_VENDOR_ID       0x1234
#define DMA_DEVICE_ID       0xA100
#define DMA_BUF_SIZE        (1 * MiB)
#define BAR0_SIZE           (4 * KiB)

struct PCIeDMAState {
    PCIDevice parent_obj;

    MemoryRegion bar0;          /* control registers */
    MemoryRegion bar1;          /* device internal buffer */

    /* register state */
    uint32_t dma_src;
    uint32_t dma_src_hi;
    uint32_t dma_dst;
    uint32_t dma_len;
    uint32_t dma_status;
    uint32_t dma_irq_mask;

    /* device buffer */
    uint8_t  dev_buf[DMA_BUF_SIZE];
};

/* ── helpers ─────────────────────────────────────────────────────────── */

static void pcie_dma_raise_irq(PCIeDMAState *s)
{
    if (s->dma_irq_mask & 0x1) {
        msi_notify(&s->parent_obj, 0);
    }
}

static void pcie_dma_do_transfer(PCIeDMAState *s, uint32_t cmd)
{
    uint64_t host_addr = ((uint64_t)s->dma_src_hi << 32) | s->dma_src;
    uint32_t dev_off   = s->dma_dst;
    uint32_t len       = s->dma_len;

    s->dma_status = STATUS_BUSY;

    /* bounds check */
    if (len == 0 || len > DMA_BUF_SIZE || dev_off + len > DMA_BUF_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "pcie_dma: invalid transfer len=%u dev_off=%u\n", len, dev_off);
        s->dma_status = STATUS_ERROR;
        pcie_dma_raise_irq(s);
        return;
    }

    if (cmd == CMD_H2D) {
        /* host memory → device buffer */
        pci_dma_read(&s->parent_obj, host_addr,
                     s->dev_buf + dev_off, len);
    } else if (cmd == CMD_D2H) {
        /* device buffer → host memory */
        pci_dma_write(&s->parent_obj, host_addr,
                      s->dev_buf + dev_off, len);
    } else {
        s->dma_status = STATUS_ERROR;
        pcie_dma_raise_irq(s);
        return;
    }

    s->dma_status = STATUS_DONE;
    pcie_dma_raise_irq(s);
}

/* ── BAR0 MMIO read ──────────────────────────────────────────────────── */

static uint64_t bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIeDMAState *s = opaque;

    switch (addr) {
    case REG_DMA_SRC:      return s->dma_src;
    case REG_DMA_SRC_HI:   return s->dma_src_hi;
    case REG_DMA_DST:      return s->dma_dst;
    case REG_DMA_LEN:      return s->dma_len;
    case REG_DMA_CMD:      return 0;
    case REG_DMA_STATUS:   return s->dma_status;
    case REG_DMA_IRQ_MASK: return s->dma_irq_mask;
    case REG_DMA_IRQ_ACK:  return 0;
    default:
        qemu_log_mask(LOG_UNIMP,
            "pcie_dma: bar0 read unknown reg 0x%"HWADDR_PRIx"\n", addr);
        return 0xDEADBEEF;
    }
}

/* ── BAR0 MMIO write ─────────────────────────────────────────────────── */

static void bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIeDMAState *s = opaque;

    switch (addr) {
    case REG_DMA_SRC:
        s->dma_src = (uint32_t)val;
        break;
    case REG_DMA_SRC_HI:
        s->dma_src_hi = (uint32_t)val;
        break;
    case REG_DMA_DST:
        s->dma_dst = (uint32_t)val;
        break;
    case REG_DMA_LEN:
        s->dma_len = (uint32_t)val;
        break;
    case REG_DMA_CMD:
        if (s->dma_status == STATUS_BUSY) {
            qemu_log_mask(LOG_GUEST_ERROR, "pcie_dma: command while busy\n");
            break;
        }
        pcie_dma_do_transfer(s, (uint32_t)val);
        break;
    case REG_DMA_IRQ_MASK:
        s->dma_irq_mask = (uint32_t)val & 0x1;
        break;
    case REG_DMA_IRQ_ACK:
        if (val & 0x1) {
            s->dma_status = STATUS_IDLE;
            
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
            "pcie_dma: bar0 write unknown reg 0x%"HWADDR_PRIx"\n", addr);
    }
}

static const MemoryRegionOps bar0_ops = {
    .read       = bar0_read,
    .write      = bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid      = { .min_access_size = 4, .max_access_size = 4 },
};

/* ── PCI realise / unrealise ─────────────────────────────────────────── */

static void pcie_dma_realize(PCIDevice *pdev, Error **errp)
{
    PCIeDMAState *s = PCIE_DMA(pdev);
    uint8_t *cfg = pdev->config;

    /* PCI class: simple communication controller */
    pci_set_word(cfg + PCI_CLASS_DEVICE, 0x0780);

    /* BAR0: 4 KB MMIO for registers */
    memory_region_init_io(&s->bar0, OBJECT(s), &bar0_ops, s,
                          "pcie-dma-bar0", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    /* BAR1: 1 MB MMIO for device buffer (RAM-backed) */
    memory_region_init_ram(&s->bar1, OBJECT(s),
                           "pcie-dma-bar1", DMA_BUF_SIZE, errp);
    if (*errp) return;
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar1);

    /* enable MSI */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        return;
    }

    s->dma_status   = STATUS_IDLE;
    s->dma_irq_mask = 0;
    memset(s->dev_buf, 0, sizeof(s->dev_buf));
}

static void pcie_dma_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void pcie_dma_reset(DeviceState *dev)
{
    PCIeDMAState *s = PCIE_DMA(dev);
    s->dma_src      = 0;
    s->dma_src_hi   = 0;
    s->dma_dst      = 0;
    s->dma_len      = 0;
    s->dma_status   = STATUS_IDLE;
    s->dma_irq_mask = 0;
}

/* ── class init ──────────────────────────────────────────────────────── */

static void pcie_dma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass    *dc  = DEVICE_CLASS(klass);
    PCIDeviceClass *k   = PCI_DEVICE_CLASS(klass);

    k->realize   = pcie_dma_realize;
    k->exit      = pcie_dma_uninit;
    k->vendor_id = DMA_VENDOR_ID;
    k->device_id = DMA_DEVICE_ID;
    k->revision  = 0x01;
    k->class_id  = 0x0780;

    dc->reset    = pcie_dma_reset;
    dc->desc     = "Simple PCIe DMA Engine";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo pcie_dma_info = {
    .name          = TYPE_PCIE_DMA,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIeDMAState),
    .class_init    = pcie_dma_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    },
};

static void pcie_dma_register_types(void)
{
    type_register_static(&pcie_dma_info);
}
type_init(pcie_dma_register_types)
