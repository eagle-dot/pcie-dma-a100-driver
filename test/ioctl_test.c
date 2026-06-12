#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "pcie_dma_ioctl.h"

#define DEV_PATH "/dev/pcie_dma0"
#define PASS "\033[32mPASS\033[0m"
#define FAIL "\033[31mFAIL\033[0m"

static const char *status_str(uint32_t s)
{
    switch (s) {
    case DMA_HW_STATUS_IDLE:  return "IDLE";
    case DMA_HW_STATUS_BUSY:  return "BUSY";
    case DMA_HW_STATUS_DONE:  return "DONE";
    case DMA_HW_STATUS_ERROR: return "ERROR";
    default:                  return "UNKNOWN";
    }
}

static int check(const char *label, int ret)
{
    if (ret < 0) {
        printf("  %-30s [%s] errno=%d\n", label, FAIL, errno);
        return 0;
    }
    printf("  %-30s [%s]\n", label, PASS);
    return 1;
}

int main(void)
{
    int fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) { perror("open " DEV_PATH); return 1; }

    printf("\n=== DMA_IOCTL_GET_INFO ===\n");
    struct dma_ioctl_info info;
    if (check("GET_INFO", ioctl(fd, DMA_IOCTL_GET_INFO, &info))) {
        printf("  vendor_id    : 0x%04x\n", info.vendor_id);
        printf("  device_id    : 0x%04x\n", info.device_id);
        printf("  buf_size     : %u bytes\n", info.buf_size);
        printf("  buf_phys_addr: 0x%llx\n", (unsigned long long)info.buf_phys_addr);
        printf("  bar0_start   : 0x%llx\n", (unsigned long long)info.bar0_start);
        printf("  bar0_size    : %llu bytes\n", (unsigned long long)info.bar0_size);
        printf("  hw_status    : %s (%u)\n", status_str(info.hw_status), info.hw_status);
    }

    printf("\n=== DMA_IOCTL_GET_STATUS ===\n");
    uint32_t status;
    if (check("GET_STATUS", ioctl(fd, DMA_IOCTL_GET_STATUS, &status)))
        printf("  status: %s (%u)\n", status_str(status), status);

    printf("\n=== Generate some traffic ===\n");
    uint8_t *buf = malloc(4096);
    memset(buf, 0x5A, 4096);
    check("write 4K H2D", write(fd, buf, 4096) == 4096 ? 0 : -1);
    check("read  4K D2H", read(fd, buf, 4096)  == 4096 ? 0 : -1);
    free(buf);

    printf("\n=== DMA_IOCTL_GET_STATS ===\n");
    struct dma_ioctl_stats stats;
    if (check("GET_STATS", ioctl(fd, DMA_IOCTL_GET_STATS, &stats))) {
        printf("  bytes_written  : %llu\n", (unsigned long long)stats.bytes_written);
        printf("  bytes_read     : %llu\n", (unsigned long long)stats.bytes_read);
        printf("  irq_count      : %llu\n", (unsigned long long)stats.irq_count);
        printf("  transfer_count : %llu\n", (unsigned long long)stats.transfer_count);
        printf("  last_latency_us: %llu\n", (unsigned long long)stats.last_latency_us);
        printf("  avg_latency_us : %llu\n", (unsigned long long)stats.avg_latency_us);
    }

    printf("\n=== DMA_IOCTL_RESET_STATS ===\n");
    check("RESET_STATS", ioctl(fd, DMA_IOCTL_RESET_STATS));
    struct dma_ioctl_stats zeroed;
    if (check("GET_STATS after reset", ioctl(fd, DMA_IOCTL_GET_STATS, &zeroed))) {
        int ok = !zeroed.bytes_written && !zeroed.bytes_read &&
                 !zeroed.irq_count && !zeroed.transfer_count;
        printf("  counters zeroed: [%s]\n", ok ? PASS : FAIL);
    }

    printf("\n=== DMA_IOCTL_RESET_DEV ===\n");
    check("RESET_DEV", ioctl(fd, DMA_IOCTL_RESET_DEV));
    uint32_t status_after;
    if (check("GET_STATUS after reset", ioctl(fd, DMA_IOCTL_GET_STATUS, &status_after)))
        printf("  status: %s (%u)\n", status_str(status_after), status_after);

    printf("\n");
    close(fd);
    return 0;
}
