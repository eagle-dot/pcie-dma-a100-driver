/* pcie_dma_driver.c v0.3 - PCIe DMA driver with sysfs stats + info */
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
#include <linux/ktime.h>
#include <linux/atomic.h>
#define DRIVER_NAME      "pcie_dma"
#define DMA_VENDOR_ID    0x1234
#define DMA_DEVICE_ID    0xDEAD
#define REG_DMA_SRC      0x00
#define REG_DMA_SRC_HI   0x04
#define REG_DMA_DST      0x08
#define REG_DMA_LEN      0x0C
#define REG_DMA_CMD      0x10
#define REG_DMA_STATUS   0x14
#define REG_DMA_IRQ_MASK 0x18
#define REG_DMA_IRQ_ACK  0x1C
#define CMD_H2D          0x01
#define CMD_D2H          0x02
#define STATUS_DONE      0x02
#define STATUS_ERROR     0x03
#define DMA_BUF_SIZE     (1 * 1024 * 1024)
#define MAX_TRANSFER_SIZE DMA_BUF_SIZE
struct dma_stats {
    atomic64_t bytes_written;
    atomic64_t bytes_read;
    atomic64_t irq_count;
    atomic64_t transfer_count;
    atomic64_t total_latency_us;
    atomic64_t last_latency_us;
};
struct dma_dev {
    struct pci_dev   *pdev;
    void __iomem     *bar0;
    void             *dma_vaddr;
    dma_addr_t        dma_paddr;
    struct completion dma_done;
    struct mutex      lock;
    struct cdev       cdev;
    dev_t             devno;
    struct class     *cls;
    struct device    *dev;
    struct dma_stats  stats;
};
static inline void reg_write(struct dma_dev *d, u32 off, u32 val)
{ iowrite32(val, d->bar0 + off); }
static inline u32 reg_read(struct dma_dev *d, u32 off)
{ return ioread32(d->bar0 + off); }
static int program_and_fire(struct dma_dev *d, u32 cmd, size_t len, u32 dev_off)
{
    u64 phys = (u64)d->dma_paddr;
    ktime_t t0, t1; s64 lat;
    reg_write(d, REG_DMA_IRQ_MASK, 0x1);
    reg_write(d, REG_DMA_SRC,    (u32)(phys & 0xFFFFFFFF));
    reg_write(d, REG_DMA_SRC_HI, (u32)(phys >> 32));
    reg_write(d, REG_DMA_DST,    dev_off);
    reg_write(d, REG_DMA_LEN,    (u32)len);
    reinit_completion(&d->dma_done);
    t0 = ktime_get();
    reg_write(d, REG_DMA_CMD, cmd);
    if (!wait_for_completion_timeout(&d->dma_done, msecs_to_jiffies(2000))) {
        dev_err(&d->pdev->dev, "DMA timeout\n"); return -ETIMEDOUT; }
    t1 = ktime_get(); lat = ktime_to_us(ktime_sub(t1,t0));
    if (reg_read(d, REG_DMA_STATUS) == STATUS_ERROR) return -EIO;
    atomic64_inc(&d->stats.transfer_count);
    atomic64_set(&d->stats.last_latency_us, lat);
    atomic64_add(lat, &d->stats.total_latency_us);
    if (cmd == CMD_H2D) atomic64_add(len, &d->stats.bytes_written);
    else atomic64_add(len, &d->stats.bytes_read);
    return 0;
}
static irqreturn_t dma_irq_handler(int irq, void *data)
{
    struct dma_dev *d = data;
    u32 status = reg_read(d, REG_DMA_STATUS);
    if (status == STATUS_DONE || status == STATUS_ERROR) {
        reg_write(d, REG_DMA_IRQ_ACK, 0x1);
        atomic64_inc(&d->stats.irq_count);
        complete(&d->dma_done); return IRQ_HANDLED; }
    return IRQ_NONE;
}
static int dma_open(struct inode *i, struct file *f)
{ f->private_data = container_of(i->i_cdev, struct dma_dev, cdev); return 0; }
static int dma_release(struct inode *i, struct file *f) { return 0; }
static ssize_t dma_write(struct file *f, const char __user *u, size_t n, loff_t *p)
{
    struct dma_dev *d = f->private_data; int ret;
    if (!n || n > MAX_TRANSFER_SIZE) return -EINVAL;
    if (mutex_lock_interruptible(&d->lock)) return -ERESTARTSYS;
    ret = copy_from_user(d->dma_vaddr, u, n) ? -EFAULT :
           program_and_fire(d, CMD_H2D, n, 0) ?: (int)n;
    mutex_unlock(&d->lock); return ret;
}
static ssize_t dma_read(struct file *f, char __user *u, size_t n, loff_t *p)
{
    struct dma_dev *d = f->private_data; int ret;
    if (!n || n > MAX_TRANSFER_SIZE) return -EINVAL;
    if (mutex_lock_interruptible(&d->lock)) return -ERESTARTSYS;
    ret = program_and_fire(d, CMD_D2H, n, 0);
    if (!ret) ret = copy_to_user(u, d->dma_vaddr, n) ? -EFAULT : (int)n;
    mutex_unlock(&d->lock); return ret;
}
static const struct file_operations dma_fops = {
    .owner   = THIS_MODULE,
    .open    = dma_open,
    .release = dma_release,
    .read    = dma_read,
    .write   = dma_write,
};
/* --- sysfs dma_stats --- */
#define STAT_SHOW(name, field) \
static ssize_t name##_show(struct device *dev, struct device_attribute *a, char *buf) \
{ struct dma_dev *d = pci_get_drvdata(to_pci_dev(dev)); \
  return sysfs_emit(buf, "%lld\n", atomic64_read(&d->stats.field)); } \
static DEVICE_ATTR_RO(name);
STAT_SHOW(bytes_written,   bytes_written)
STAT_SHOW(bytes_read,      bytes_read)
STAT_SHOW(irq_count,       irq_count)
STAT_SHOW(last_latency_us, last_latency_us)
static ssize_t avg_latency_us_show(struct device *dev, struct device_attribute *a, char *buf)
{
    struct dma_dev *d = pci_get_drvdata(to_pci_dev(dev));
    s64 tc = atomic64_read(&d->stats.transfer_count);
    s64 avg = tc ? atomic64_read(&d->stats.total_latency_us) / tc : 0;
    return sysfs_emit(buf, "%lld\n", avg);
}
static DEVICE_ATTR_RO(avg_latency_us);
static ssize_t reset_stats_store(struct device *dev, struct device_attribute *a,
                                  const char *buf, size_t count)
{
    struct dma_dev *d = pci_get_drvdata(to_pci_dev(dev));
    atomic64_set(&d->stats.bytes_written,   0);
    atomic64_set(&d->stats.bytes_read,      0);
    atomic64_set(&d->stats.irq_count,       0);
    atomic64_set(&d->stats.transfer_count,  0);
    atomic64_set(&d->stats.total_latency_us,0);
    atomic64_set(&d->stats.last_latency_us, 0);
    dev_info(dev,"stats reset\n"); return count;
}
static DEVICE_ATTR_WO(reset_stats);
static struct attribute *dma_stat_attrs[] = {
    &dev_attr_bytes_written.attr,  &dev_attr_bytes_read.attr,
    &dev_attr_irq_count.attr,      &dev_attr_last_latency_us.attr,
    &dev_attr_avg_latency_us.attr, &dev_attr_reset_stats.attr, NULL };
static const struct attribute_group dma_stat_group = { .name="dma_stats", .attrs=dma_stat_attrs };
/* --- sysfs dma_info --- */
static ssize_t buf_size_show(struct device *dev, struct device_attribute *a, char *buf)
{
    return sysfs_emit(buf, "%u\n", DMA_BUF_SIZE);
}
static DEVICE_ATTR_RO(buf_size);
static ssize_t buf_phys_addr_show(struct device *dev, struct device_attribute *a, char *buf)
{
    struct dma_dev *d = pci_get_drvdata(to_pci_dev(dev));
    return sysfs_emit(buf, "0x%llx\n", (u64)d->dma_paddr);
}
static DEVICE_ATTR_RO(buf_phys_addr);
static ssize_t bar0_start_show(struct device *dev, struct device_attribute *a, char *buf)
{
    struct pci_dev *pdev = to_pci_dev(dev);
    return sysfs_emit(buf, "0x%llx\n", (u64)pci_resource_start(pdev, 0));
}
static DEVICE_ATTR_RO(bar0_start);
static ssize_t bar0_size_show(struct device *dev, struct device_attribute *a, char *buf)
{
    struct pci_dev *pdev = to_pci_dev(dev);
    return sysfs_emit(buf, "%llu\n", (u64)pci_resource_len(pdev, 0));
}
static DEVICE_ATTR_RO(bar0_size);
static ssize_t vendor_id_show(struct device *dev, struct device_attribute *a, char *buf)
{
    return sysfs_emit(buf, "0x%04x\n", DMA_VENDOR_ID);
}
static DEVICE_ATTR_RO(vendor_id);
static ssize_t device_id_show(struct device *dev, struct device_attribute *a, char *buf)
{
    return sysfs_emit(buf, "0x%04x\n", DMA_DEVICE_ID);
}
static DEVICE_ATTR_RO(device_id);
static struct attribute *dma_info_attrs[] = {
    &dev_attr_buf_size.attr,
    &dev_attr_buf_phys_addr.attr,
    &dev_attr_bar0_start.attr,
    &dev_attr_bar0_size.attr,
    &dev_attr_vendor_id.attr,
    &dev_attr_device_id.attr,
    NULL,
};
static const struct attribute_group dma_info_group = { .name="dma_info", .attrs=dma_info_attrs };
static int dma_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct dma_dev *d; int ret;
    d=devm_kzalloc(&pdev->dev,sizeof(*d),GFP_KERNEL); if(!d) return -ENOMEM;
    d->pdev=pdev; mutex_init(&d->lock); init_completion(&d->dma_done);
    pci_set_drvdata(pdev,d);
    ret=pci_enable_device(pdev); if(ret) return ret;
    pci_set_master(pdev);
    ret=dma_set_mask_and_coherent(&pdev->dev,DMA_BIT_MASK(64));
    if(ret) ret=dma_set_mask_and_coherent(&pdev->dev,DMA_BIT_MASK(32));
    if(ret) goto err_disable;
    ret=pci_request_regions(pdev,DRIVER_NAME); if(ret) goto err_disable;
    d->bar0=pci_iomap(pdev,0,0); if(!d->bar0){ret=-ENOMEM;goto err_release;}
    d->dma_vaddr=dma_alloc_coherent(&pdev->dev,DMA_BUF_SIZE,&d->dma_paddr,GFP_KERNEL);
    if(!d->dma_vaddr){ret=-ENOMEM;goto err_unmap;}
    ret=pci_enable_msi(pdev); if(ret) goto err_dma;
    ret=request_irq(pdev->irq,dma_irq_handler,0,DRIVER_NAME,d);
    if(ret) goto err_msi;
    ret=alloc_chrdev_region(&d->devno,0,1,DRIVER_NAME); if(ret) goto err_irq;
    cdev_init(&d->cdev, &dma_fops); d->cdev.owner=THIS_MODULE;
    ret=cdev_add(&d->cdev,d->devno,1); if(ret) goto err_chrdev;
    d->cls=class_create(THIS_MODULE,DRIVER_NAME);
    if(IS_ERR(d->cls)){ret=PTR_ERR(d->cls);goto err_cdev;}
    d->dev=device_create(d->cls,NULL,d->devno,NULL,"pcie_dma0");
    if(IS_ERR(d->dev)){ret=PTR_ERR(d->dev);goto err_class;}
    ret=sysfs_create_group(&pdev->dev.kobj,&dma_stat_group);
    if(ret) goto err_device;
    ret=sysfs_create_group(&pdev->dev.kobj,&dma_info_group);
    if(ret) goto err_stats;
    dev_info(&pdev->dev,"ready - /dev/pcie_dma0 | sysfs: dma_stats/ dma_info/\n");
    return 0;
err_stats: sysfs_remove_group(&pdev->dev.kobj,&dma_stat_group);
err_device: device_destroy(d->cls,d->devno);
err_class: class_destroy(d->cls);
err_cdev: cdev_del(&d->cdev);
err_chrdev: unregister_chrdev_region(d->devno,1);
err_irq: free_irq(pdev->irq,d);
err_msi: pci_disable_msi(pdev);
err_dma: dma_free_coherent(&pdev->dev,DMA_BUF_SIZE,d->dma_vaddr,d->dma_paddr);
err_unmap: pci_iounmap(pdev,d->bar0);
err_release: pci_release_regions(pdev);
err_disable: pci_disable_device(pdev);
    return ret;
}
static void dma_remove(struct pci_dev *pdev)
{
    struct dma_dev *d=pci_get_drvdata(pdev);
    sysfs_remove_group(&pdev->dev.kobj,&dma_info_group);
    sysfs_remove_group(&pdev->dev.kobj,&dma_stat_group);
    device_destroy(d->cls,d->devno);
    class_destroy(d->cls);
    cdev_del(&d->cdev);
    unregister_chrdev_region(d->devno,1);
    free_irq(pdev->irq,d);
    pci_disable_msi(pdev);
    dma_free_coherent(&pdev->dev,DMA_BUF_SIZE,d->dma_vaddr,d->dma_paddr);
    pci_iounmap(pdev,d->bar0);
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    dev_info(&pdev->dev,"removed\n");
}
static const struct pci_device_id dma_id_table[] = {
    { PCI_DEVICE(DMA_VENDOR_ID, DMA_DEVICE_ID) }, { 0 }
};
MODULE_DEVICE_TABLE(pci, dma_id_table);
static struct pci_driver dma_driver = {
    .name=DRIVER_NAME, .id_table=dma_id_table,
    .probe=dma_probe, .remove=dma_remove };
module_pci_driver(dma_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("orin-dev");
MODULE_DESCRIPTION("PCIe DMA engine driver with sysfs stats + info");
MODULE_VERSION("0.3");
