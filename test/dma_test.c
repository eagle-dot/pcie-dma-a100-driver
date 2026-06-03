#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

#define DEV_PATH     "/dev/pcie_dma0"
#define TEST_SIZE    (64 * 1024)
#define BENCH_SIZE   (1024 * 1024)
#define BENCH_ITERS  10

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(void) {
    int fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) { perror("open " DEV_PATH); return 1; }

    uint8_t *wb = malloc(TEST_SIZE), *rb = malloc(TEST_SIZE);
    for (int i = 0; i < TEST_SIZE; i++) wb[i] = i & 0xFF;
    memset(rb, 0xAA, TEST_SIZE);

    printf("H2D write %d KB ... ", TEST_SIZE/1024);
    if (write(fd, wb, TEST_SIZE) != TEST_SIZE) { perror("write"); return 1; }
    printf("OK\n");

    printf("D2H read  %d KB ... ", TEST_SIZE/1024);
    if (read(fd, rb, TEST_SIZE) != TEST_SIZE) { perror("read"); return 1; }
    printf("OK\n");

    printf("Verify              ... ");
    if (memcmp(wb, rb, TEST_SIZE)) { printf("FAIL\n"); return 1; }
    printf("OK — data matches\n\n");

    uint8_t *bb = malloc(BENCH_SIZE);
    memset(bb, 0x5A, BENCH_SIZE);
    double total = 0;
    printf("H2D bench (%d MB x %d):\n", BENCH_SIZE>>20, BENCH_ITERS);
    for (int i = 0; i < BENCH_ITERS; i++) {
        double t = now();
        write(fd, bb, BENCH_SIZE);
        double mb = (BENCH_SIZE >> 20) / (now() - t);
        total += mb;
        printf("  %2d: %.1f MB/s\n", i, mb);
    }
    printf("  avg: %.1f MB/s\n\nAll tests PASSED\n", total/BENCH_ITERS);

    close(fd);
    return 0;
}
