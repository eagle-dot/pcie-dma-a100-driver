/* pcie_dma_ioctl.h — shared between kernel driver and userspace */
#ifndef PCIE_DMA_IOCTL_H
#define PCIE_DMA_IOCTL_H

#ifdef __KERNEL__
# include <linux/types.h>
# include <linux/ioctl.h>
#else
# include <stdint.h>
# include <sys/ioctl.h>
typedef uint32_t __u32;
typedef uint64_t __u64;
#endif

#define DMA_IOC_MAGIC 'd'

/* Returned by DMA_IOCTL_GET_STATS */
struct dma_ioctl_stats {
    __u64 bytes_written;
    __u64 bytes_read;
    __u64 irq_count;
    __u64 transfer_count;
    __u64 avg_latency_us;
    __u64 last_latency_us;
};

/* Returned by DMA_IOCTL_GET_INFO */
struct dma_ioctl_info {
    __u64 buf_phys_addr;
    __u64 bar0_start;
    __u64 bar0_size;
    __u32 buf_size;
    __u32 vendor_id;
    __u32 device_id;
    __u32 hw_status;    /* current REG_DMA_STATUS value */
};

/* hw_status values (mirrors REG_DMA_STATUS) */
#define DMA_HW_STATUS_IDLE  0
#define DMA_HW_STATUS_BUSY  1
#define DMA_HW_STATUS_DONE  2
#define DMA_HW_STATUS_ERROR 3

#define DMA_IOCTL_GET_STATUS  _IOR(DMA_IOC_MAGIC, 0, __u32)
#define DMA_IOCTL_GET_STATS   _IOR(DMA_IOC_MAGIC, 1, struct dma_ioctl_stats)
#define DMA_IOCTL_RESET_STATS _IO(DMA_IOC_MAGIC,  2)
#define DMA_IOCTL_GET_INFO    _IOR(DMA_IOC_MAGIC, 3, struct dma_ioctl_info)
#define DMA_IOCTL_RESET_DEV   _IO(DMA_IOC_MAGIC,  4)

#endif /* PCIE_DMA_IOCTL_H */
