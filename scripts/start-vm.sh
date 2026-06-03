#!/bin/bash
set -e
BASE="$HOME/vm-driver-dev"
QEMU="$BASE/qemu-build/bin/qemu-system-aarch64"

[ -c /dev/kvm ] || { echo "ERROR: /dev/kvm not found"; exit 1; }
[ -x "$QEMU" ]  || { echo "ERROR: QEMU not found at $QEMU"; exit 1; }

exec $QEMU \
  -machine virt,accel=kvm,gic-version=3 \
  -cpu host \
  -smp 4,cores=4,threads=1 \
  -m 2G \
  \
  -drive if=pflash,format=raw,readonly=on,file=$BASE/firmware/QEMU_EFI.img \
  -drive if=pflash,format=raw,file=$BASE/firmware/varstore.img \
  \
  -drive file=$BASE/vm/jammy-server-cloudimg-arm64.img,format=qcow2,if=virtio,cache=writeback \
  -drive file=$BASE/vm/seed.img,format=raw,if=virtio \
  \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=net0 \
  \
  -virtfs local,path=$BASE/driver_share,mount_tag=drvshare,security_model=mapped-xattr \
  \
  -device pcie-dma \
  \
  -nographic \
  -serial mon:stdio
