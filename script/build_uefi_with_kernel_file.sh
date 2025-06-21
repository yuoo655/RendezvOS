#!/bin/bash

# RendezvOS UEFI Stub with Kernel File Build Script (using working GNU-EFI method)

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
KERNEL_PATH="$1"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

if [ -z "$KERNEL_PATH" ]; then
    echo -e "${RED}Usage: $0 <kernel_binary_path>${NC}"
    echo -e "${YELLOW}Example: $0 /path/to/kernel.bin${NC}"
    exit 1
fi

if [ ! -f "$KERNEL_PATH" ]; then
    echo -e "${RED}Kernel file not found: $KERNEL_PATH${NC}"
    exit 1
fi

echo -e "${GREEN}Building RendezvOS UEFI stub with kernel file...${NC}"
echo -e "${YELLOW}Kernel: $KERNEL_PATH${NC}"

# Use the working UEFI stub build script
echo -e "${GREEN}Building UEFI stub using working method...${NC}"
script/build_uefi.sh

if [ $? -ne 0 ]; then
    echo -e "${RED}Failed to build UEFI stub${NC}"
    exit 1
fi

echo -e "${GREEN}Creating disk image with kernel file...${NC}"

# Create larger disk image for kernel file (using working method)
DISK_IMAGE="$BUILD_DIR/rendezvos_uefi_with_kernel.img"
dd if=/dev/zero of="$DISK_IMAGE" bs=1M count=128 2>/dev/null
mkfs.fat -F32 -s1 -S512 "$DISK_IMAGE"

# Mount and copy files
MOUNT_POINT=$(mktemp -d)
sudo mount -o loop "$DISK_IMAGE" "$MOUNT_POINT"
sudo mkdir -p "$MOUNT_POINT/EFI/BOOT"
sudo cp "$BUILD_DIR/rendezv_uefi.efi" "$MOUNT_POINT/EFI/BOOT/BOOTX64.EFI"
sudo cp "$KERNEL_PATH" "$MOUNT_POINT/kernel.bin"
sudo umount "$MOUNT_POINT"
rmdir "$MOUNT_POINT"

# Create VHDX if possible
if command -v qemu-img &> /dev/null; then
    echo -e "${GREEN}Creating VHDX image...${NC}"
    qemu-img convert -f raw -O vhdx "$DISK_IMAGE" "$BUILD_DIR/rendezvos_uefi_with_kernel.vhdx"
fi

KERNEL_SIZE=$(stat -c%s "$KERNEL_PATH")
EFI_SIZE=$(stat -c%s "$BUILD_DIR/rendezv_uefi.efi")

echo -e "${GREEN}Build complete!${NC}"
echo -e "${GREEN}Files created:${NC}"
echo -e "  Kernel size: $KERNEL_SIZE bytes"
echo -e "  EFI application: $BUILD_DIR/rendezv_uefi.efi ($EFI_SIZE bytes)"
echo -e "  Disk image: $DISK_IMAGE"
if [ -f "$BUILD_DIR/rendezvos_uefi_with_kernel.vhdx" ]; then
    echo -e "  VHDX image: $BUILD_DIR/rendezvos_uefi_with_kernel.vhdx"
fi

echo -e "${GREEN}To test with QEMU:${NC}"
echo -e "  qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -hda $DISK_IMAGE -m 128M -serial stdio"

echo -e "${YELLOW}Note: The kernel file 'kernel.bin' is stored separately in the FAT32 filesystem${NC}"
echo -e "${YELLOW}The UEFI stub can be extended to read and load this kernel file${NC}" 