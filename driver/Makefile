obj-m += pcie_dma_driver.o
KDIR ?= /lib/modules/$(shell uname -r)/build
all:
	make -C $(KDIR) M=$(PWD) modules
clean:
	make -C $(KDIR) M=$(PWD) clean
reload:
	sudo rmmod pcie_dma_driver 2>/dev/null || true
	sudo insmod pcie_dma_driver.ko
	sudo dmesg | tail -10
