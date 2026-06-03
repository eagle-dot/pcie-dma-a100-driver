#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/io.h>

#define DRIVER_NAME     "pcie_dma"
#define DMA_VENDOR_ID   0x1234
#define DMA_DEVICE_ID   0xDEAD

#define REG_DMA_SRC         0x00
#define REG_DMA_SRC_HI      0x04
#define REG_DMA_DST         0x08
#define REG_DMA_LEN         0x0C
#define REG_DMA_CMD         0x10
#define REG_DMA_STATUS      0x14
#define REG_DMA_IRQ_MASK    0x18
#define REG_DMA_IRQ_ACK     0x1C

#define CMD_H2D             0x01
#define CMD_D2H             0x02
#define STATUS_IDLE         0x00
#define STATUS_BUSY         0x01
#define STATUS_DONE         0x02
#define STATUS_ERROR        0x03
#define DMA_BUF_SIZE        (1 * 1024 * 1024)
#define MAX_TRANSFER_SIZE   DMA_BUF_SIZE

struct dma_dev {
    struct pci_dev      *pdev;
    void __iomem        *bar0;
    void                *dma_vaddr;
    dma_addr_t           dma_paddr;
    struct completion    dma_done;
    struct mutex         lock;
    struct cdev          cdev;
    dev_t                devno;
    struct class        *cls;
    struct device       *dev;
};

static inline void reg_write(struct dma_dev *d, u32 off, u32 val)
{ iowrite32(val, d->bar0 + off); }

static inline u32 reg_read(struct dma_dev *d, u32 off)
{ return ioread32(d->bar0 + off); }

static int program_and_fire(struct dma_dev *d, u32 cmd, size_t len, u32 dev_off)
{
    u64 phys = (u64)d->dma_paddr;
    reg_write(d, REG_DMA_IRQ_MASK, 0x1);
    reg_write(d, REG_DMA_SRC,    (u32)(phys & 0xFFFFFFFF));
    reg_write(d, REG_DMA_SRC_HI, (u32)(phys >> 32));
    reg_write(d, REG_DMA_DST,    dev_off);
    reg_write(d, REG_DMA_LEN,    (u32)len);
    reinit_completion(&d->dma_done);
    reg_write(d, REG_DMA_CMD, cmd);
    if (!wait_for_completion_timeout(&d->dma_done, msecs_to_jiffies(2000))) {
        dev_err(&d->pdev->dev, "DMA timeout\n");
        return -ETIMEDOUT;
    }
    if (reg_read(d, REG_DMA_STATUS) == STATUS_ERROR) {
        dev_err(&d->pdev->dev, "DMA error\n");
        return -EIO;
    }
    return 0;
}

static irqreturn_t dma_irq_handler(int irq, void *data)
{
    struct dma_dev *d = data;
    u32 status = reg_read(d, REG_DMA_STATUS);
    if (status == STATUS_DONE || status == STATUS_ERROR) {
        reg_write(d, REG_DMA_IRQ_ACK, 0x1);
        complete(&d->dma_done);
        return IRQ_HANDLED;
    }
    return IRQ_NONE;
}

static int dma_open(struct inode *inode, struct file *filp)
{
    filp->private_data = container_of(inode->i_cdev, struct dma_dev, cdev);
    return 0;
}
static int dma_release(struct inode *inode, struct file *filp) { return 0; }

static ssize_t dma_write(struct file *filp, const char __user *ubuf,
                         size_t count, loff_t *ppos)
{
    struct dma_dev *d = filp->private_data;
    int ret;
    if (count == 0 || count > MAX_TRANSFER_SIZE) return -EINVAL;
    if (mutex_lock_interruptible(&d->lock)) return -ERESTARTSYS;
    if (copy_from_user(d->dma_vaddr, ubuf, count)) { ret = -EFAULT; goto out; }
    ret = program_and_fire(d, CMD_H2D, count, 0);
    if (!ret) ret = count;
out:
    mutex_unlock(&d->lock);
    return ret;
}

static ssize_t dma_read(struct file *filp, char __user *ubuf,
                        size_t count, loff_t *ppos)
{
    struct dma_dev *d = filp->private_data;
    int ret;
    if (count == 0 || count > MAX_TRANSFER_SIZE) return -EINVAL;
    if (mutex_lock_interruptible(&d->lock)) return -ERESTARTSYS;
    ret = program_and_fire(d, CMD_D2H, count, 0);
    if (ret) goto out;
    if (copy_to_user(ubuf, d->dma_vaddr, count)) { ret = -EFAULT; goto out; }
    ret = count;
out:
    mutex_unlock(&d->lock);
    return ret;
}

static const struct file_operations dma_fops = {
    .owner   = THIS_MODULE,
    .open    = dma_open,
    .release = dma_release,
    .write   = dma_write,
    .read    = dma_read,
};

static int dma_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct dma_dev *d;
    int ret;

    d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
    if (!d) return -ENOMEM;
    d->pdev = pdev;
    mutex_init(&d->lock);
    init_completion(&d->dma_done);
    pci_set_drvdata(pdev, d);

    ret = pci_enable_device(pdev);
    if (ret) return ret;
    pci_set_master(pdev);

    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
    if (ret) ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
    if (ret) { dev_err(&pdev->dev, "no DMA mask\n"); goto err_disable; }

    ret = pci_request_regions(pdev, DRIVER_NAME);
    if (ret) goto err_disable;

    d->bar0 = pci_iomap(pdev, 0, 0);
    if (!d->bar0) { ret = -ENOMEM; goto err_release; }

    d->dma_vaddr = dma_alloc_coherent(&pdev->dev, DMA_BUF_SIZE,
                                       &d->dma_paddr, GFP_KERNEL);
    if (!d->dma_vaddr) { ret = -ENOMEM; goto err_unmap; }

    ret = pci_enable_msi(pdev);
    if (ret) goto err_dma_free;

    ret = request_irq(pdev->irq, dma_irq_handler, 0, DRIVER_NAME, d);
    if (ret) goto err_msi;

    ret = alloc_chrdev_region(&d->devno, 0, 1, DRIVER_NAME);
    if (ret) goto err_irq;

    cdev_init(&d->cdev, &dma_fops);
    d->cdev.owner = THIS_MODULE;
    ret = cdev_add(&d->cdev, d->devno, 1);
    if (ret) goto err_chrdev;

    d->cls = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR(d->cls)) { ret = PTR_ERR(d->cls); goto err_cdev; }

    d->dev = device_create(d->cls, NULL, d->devno, NULL, "pcie_dma0");
    if (IS_ERR(d->dev)) { ret = PTR_ERR(d->dev); goto err_class; }

    dev_info(&pdev->dev, "ready — /dev/pcie_dma0\n");
    return 0;

err_class:    class_destroy(d->cls);
err_cdev:     cdev_del(&d->cdev);
err_chrdev:   unregister_chrdev_region(d->devno, 1);
err_irq:      free_irq(pdev->irq, d);
err_msi:      pci_disable_msi(pdev);
err_dma_free: dma_free_coherent(&pdev->dev, DMA_BUF_SIZE, d->dma_vaddr, d->dma_paddr);
err_unmap:    pci_iounmap(pdev, d->bar0);
err_release:  pci_release_regions(pdev);
err_disable:  pci_disable_device(pdev);
    return ret;
}

static void dma_remove(struct pci_dev *pdev)
{
    struct dma_dev *d = pci_get_drvdata(pdev);
    device_destroy(d->cls, d->devno);
    class_destroy(d->cls);
    cdev_del(&d->cdev);
    unregister_chrdev_region(d->devno, 1);
    free_irq(pdev->irq, d);
    pci_disable_msi(pdev);
    dma_free_coherent(&pdev->dev, DMA_BUF_SIZE, d->dma_vaddr, d->dma_paddr);
    pci_iounmap(pdev, d->bar0);
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    dev_info(&pdev->dev, "removed\n");
}

static const struct pci_device_id dma_id_table[] = {
    { PCI_DEVICE(DMA_VENDOR_ID, DMA_DEVICE_ID) },
    { }
};
MODULE_DEVICE_TABLE(pci, dma_id_table);

static struct pci_driver dma_driver = {
    .name     = DRIVER_NAME,
    .id_table = dma_id_table,
    .probe    = dma_probe,
    .remove   = dma_remove,
};

module_pci_driver(dma_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("orin-dev");
MODULE_DESCRIPTION("PCIe DMA engine driver");
MODULE_VERSION("0.1");
