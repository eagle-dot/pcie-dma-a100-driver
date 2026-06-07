# PCIe DMA Driver

A custom PCIe DMA kernel driver and matching QEMU device model for Linux driver
development on **Jetson Orin Nano** (JetPack 6.2). Includes a full sysfs
interface (`dma_stats/` and `dma_info/`) and a userspace test program that
validates H2D and D2H transfers at ~2600 MB/s average throughput.

The device is a **PCIe endpoint with x16 link width and Gen2 (5 GT/s) speed**.

This is a refined version of PCI-based DMA  https://github.com/eagle-dot/pcie-dma-driver

```
/sys/bus/pci/devices/0000:00:03.0/
├── dma_stats/
│   ├── bytes_written
│   ├── bytes_read
│   ├── irq_count
│   ├── last_latency_us
│   ├── avg_latency_us
│   └── reset_stats
└── dma_info/
    ├── buf_size
    ├── buf_phys_addr
    ├── bar0_start
    ├── bar0_size
    ├── vendor_id
    └── device_id
```

---

## Hardware and Software Requirements

| | Requirement |
|--|-------------|
| **Host hardware** | Jetson Orin Nano (tested on JetPack 6.2 / L4T R36.4.x) |
| **Host OS** | Ubuntu 22.04 |
| **Host kernel** | 5.15.x-tegra |
| **KVM** | Must be enabled (`/dev/kvm` present) |
| **QEMU** | Built from source v8.2.0 (instructions below) |
| **Guest OS** | Ubuntu 22.04 cloud image (arm64) |
| **Guest kernel** | 5.15.0-181-generic |
| **Disk space** | ~5 GB free |
| **Time** | ~20 minutes end to end |

> This project has been tested on Jetson Orin Nano unit. It will not
> work on x86 hosts without modifications to the VM launch script and driver.

---

## Repository Layout

```
pcie-dma-driver/
├── device/
│   └── pcie_dma.c          QEMU device model (inject into QEMU source tree)
├── driver/
│   ├── pcie_dma_driver.c   Linux kernel module
│   └── Makefile
├── test/
│   └── dma_test.c          Userspace validation test
└── scripts/
    └── start-vm.sh         QEMU VM launch script
```

---

## Register Map

The device exposes 8 MMIO registers in BAR0:

| Offset | Name | Description |
|--------|------|-------------|
| `0x00` | `REG_DMA_SRC` | Host DMA source address (low 32 bits) |
| `0x04` | `REG_DMA_SRC_HI` | Host DMA source address (high 32 bits) |
| `0x08` | `REG_DMA_DST` | Device buffer destination offset |
| `0x0C` | `REG_DMA_LEN` | Transfer length in bytes |
| `0x10` | `REG_DMA_CMD` | Command: `0x01`=H2D, `0x02`=D2H |
| `0x14` | `REG_DMA_STATUS` | Status: `0`=idle, `1`=busy, `2`=done, `3`=error |
| `0x18` | `REG_DMA_IRQ_MASK` | `1` = enable MSI on completion |
| `0x1C` | `REG_DMA_IRQ_ACK` | Write `1` to clear interrupt |

**Vendor ID:** `0x1234` — **Device ID:** `0xA100`

---

## Step 1 — Verify KVM on the host

```bash
ls /dev/kvm && echo "KVM ready"
zcat /proc/config.gz | grep "CONFIG_KVM="
uname -r
# Expected: 5.15.148-tegra
```

---

## Step 2 — Install host dependencies

```bash
sudo apt update
sudo apt install -y \
  qemu-system-aarch64 qemu-utils qemu-efi-aarch64 \
  cloud-image-utils git libglib2.0-dev libpixman-1-dev \
  ninja-build flex bison libslirp-dev \
  pkg-config libcap-ng-dev build-essential

sudo usermod -aG kvm $USER
newgrp kvm
```

---

## Step 3 — Set up workspace and firmware

```bash
mkdir -p ~/vm-driver-dev/{vm,firmware,driver_share/{driver,test,qemu-dma-dev}}

cp /usr/share/qemu-efi-aarch64/QEMU_EFI.fd ~/vm-driver-dev/firmware/QEMU_EFI.img
truncate -s 64M ~/vm-driver-dev/firmware/QEMU_EFI.img
dd if=/dev/zero of=~/vm-driver-dev/firmware/varstore.img bs=1M count=64
```

---

## Step 4 — Create the guest disk image

```bash
cd ~/vm-driver-dev/vm
wget -c https://cloud-images.ubuntu.com/jammy/current/jammy-server-cloudimg-arm64.img
qemu-img resize jammy-server-cloudimg-arm64.img 10G

cat > user-data << 'EOF'
#cloud-config
hostname: vmguest
users:
  - name: dev
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    lock_passwd: false
    plain_text_passwd: 'dev123'
ssh_pwauth: true
EOF

printf "instance-id: orin-vm01\nlocal-hostname: vmguest\n" > meta-data
cloud-localds seed.img user-data meta-data
```

---

## Step 5 — Copy driver files from this repo

```bash
cp driver/pcie_dma_driver.c ~/vm-driver-dev/driver_share/driver/
cp driver/Makefile           ~/vm-driver-dev/driver_share/driver/
cp test/dma_test.c           ~/vm-driver-dev/driver_share/test/
cp scripts/start-vm.sh       ~/vm-driver-dev/start-vm.sh
chmod +x ~/vm-driver-dev/start-vm.sh
```

---

## Step 6 — Build QEMU with the pcie-dma device

```bash
cd ~/vm-driver-dev
git clone https://gitlab.com/qemu-project/qemu.git \
  --depth=1 --branch v8.2.0 qemu-src
cd qemu-src

# Inject the device model
cp ~/vm-driver-dev/driver_share/qemu-dma-dev/pcie_dma.c hw/misc/
# OR from this repo:
# cp /path/to/pcie-dma-driver/device/pcie_dma.c hw/misc/

echo "system_ss.add(files('pcie_dma.c'))" >> hw/misc/meson.build

# Build (use system Python if conda is active)
PYTHON=/usr/bin/python3 ./configure \
  --target-list=aarch64-softmmu \
  --enable-kvm \
  --prefix=$HOME/vm-driver-dev/qemu-build

make -j$(nproc)
make install

# Verify device is registered
~/vm-driver-dev/qemu-build/bin/qemu-system-aarch64 \
  -device ? 2>&1 | grep pcie-dma
# Expected: name "pcie-dma", bus PCI, desc "Simple PCIe DMA Engine"
```

---

## Step 7 — Start the VM

```bash
~/vm-driver-dev/start-vm.sh
```

Login: `dev` / `dev123` — first boot takes ~90 seconds.

SSH from host:
```bash
ssh -p 2222 dev@localhost
```

> **QEMU monitor shortcuts:**
> `Ctrl-A C` — toggle monitor | `Ctrl-A X` — kill VM | `quit` — graceful shutdown

---

## Step 8 — Inside the VM

```bash
# Mount the shared driver directory
sudo mkdir -p /mnt/drvshare
sudo mount -t 9p -o trans=virtio,version=9p2000.L drvshare /mnt/drvshare

# Persist across reboots
echo "drvshare /mnt/drvshare 9p trans=virtio,version=9p2000.L,nofail 0 0" \
  | sudo tee -a /etc/fstab

# Install build tools
sudo apt update && sudo apt install -y \
  linux-headers-$(uname -r) build-essential kmod

# Build the driver against the VM kernel
cp -r /mnt/drvshare/driver ~/driver-build
cd ~/driver-build
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules

# Load the driver
sudo insmod ~/driver-build/pcie_dma_driver.ko
sudo dmesg | tail -3
# Expected: pcie_dma 0000:00:03.0: ready - /dev/pcie_dma0 | sysfs: dma_stats/ dma_info/
```

> **Note:** The driver must be built inside the VM against the guest kernel
> (`5.15.0-181-generic`), not the host kernel (`5.15.148-tegra`). Building on
> the host and copying the `.ko` will result in `Invalid module format` on load.

---

## Step 9 — Run the test

```bash
gcc -O2 -o /tmp/dma_test ~/driver-build/test/dma_test.c
sudo /tmp/dma_test
# Expected: All tests PASSED, ~2600+ MB/s average
```

> `/tmp` is cleared on reboot — rebuild `dma_test` after each reboot.

---

## Step 10 — Read sysfs attributes

### Transfer statistics

```bash
SYSFS=/sys/bus/pci/devices/0000:00:03.0/dma_stats
for f in bytes_written bytes_read irq_count avg_latency_us last_latency_us; do
  printf "%-20s: %s\n" $f $(cat $SYSFS/$f)
done

# Reset counters
echo 1 | sudo tee $SYSFS/reset_stats
```

### Device info

```bash
for f in buf_size buf_phys_addr bar0_start bar0_size vendor_id device_id; do
  printf "%-15s: %s\n" "$f" \
    "$(cat /sys/bus/pci/devices/0000:00:03.0/dma_info/$f)"
done
```

---

## Development loop

```bash
# On HOST — edit driver source
nano ~/vm-driver-dev/driver_share/driver/pcie_dma_driver.c

# In VM — rebuild and reload
cd ~/driver-build
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
sudo rmmod pcie_dma_driver 2>/dev/null || true
sudo insmod ~/driver-build/pcie_dma_driver.ko
sudo dmesg | tail -5
sudo /tmp/dma_test
```

---

## Benchmark results

| Unit | Hostname | L4T | H2D avg | D2H avg |
|------|----------|-----|---------|---------|

| 1 | mobileAI2 | R36.4.7 | ~2643 MB/s | ~2616 MB/s |

> Throughput improvement on mobileAI2 reflects the PCIe x16 Gen2 endpoint
> upgrade and device ID change to `0xA100`. 

What is EMULATED (cosmetic only) 

x16 link width — we wrote values into PCIe config space registers. lspci reads those registers and reports x16. But QEMU does not simulate lanes at all — there is no actual bandwidth difference between x1 and x16 in this environment.
5 GT/s Gen2 speed — same situation, cosmetic only.

Throughput numbers — the ~2600 MB/s reflects host memory copy speed via QEMU's pci_dma_read/write, not PCIe bus bandwidth.

---

## PCIe x16 Upgrade

The device model was upgraded from conventional PCI to a **PCIe x16 Gen2
endpoint**. The following changes were made to `pcie_dma.c`:

- Added `#include "hw/pci/pcie.h"` and `#include "hw/pci/pcie_regs.h"`
- Replaced `INTERFACE_CONVENTIONAL_PCI_DEVICE` with `INTERFACE_PCIE_DEVICE`
- Added `pcie_cap_init()` to register a PCIe capability structure
- Set link capability and status registers directly to advertise x16 width
  and Gen2 (5 GT/s) speed via `pci_set_long()`/`pci_set_word()` on the
  `PCI_EXP_LNKCAP` and `PCI_EXP_LNKSTA` fields

Verify inside VM:
```bash
sudo lspci -vv | grep -A30 "1234:a100" | grep -i lnk
# Expected: LnkCap: Width x16, Speed 5GT/s
#           LnkSta: Width x16, Speed 5GT/s
```



---

## Changelog

| Date | Change |
|------|--------|
| 2026-06-06 | Changed `DMA_DEVICE_ID`  to `0xA100` in both QEMU device model and kernel driver |
| 2026-06-06 | Upgraded device from conventional PCI to PCIe x16 Gen2 endpoint (`INTERFACE_PCIE_DEVICE`) |
| 2026-06-06 | Updated benchmark results reflecting x16 upgrade (~2600 MB/s on mobileAI2) |
| 2026-06-06 | Added SSH access instructions and VM kernel build notes |

---


## License

GPL-2.0 — kernel modules must be GPL-compatible.
