#!/bin/bash

# Hello World UEFI build script using GNU-EFI

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building Hello World UEFI application...${NC}"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

# Check for GNU-EFI
if [ ! -f /usr/include/efi/efi.h ]; then
    echo -e "${RED}GNU-EFI not found. Installing...${NC}"
    apt-get update && apt-get install -y gnu-efi
    if [ $? -ne 0 ]; then
        echo -e "${RED}Failed to install GNU-EFI${NC}"
        exit 1
    fi
fi

# Set up compiler
CC="gcc"
OBJCOPY="objcopy"

# Standard GNU-EFI flags
EFI_CFLAGS="-I/usr/include/efi -I/usr/include/efi/x86_64"
EFI_CFLAGS="$EFI_CFLAGS -fno-stack-protector -fpic -fshort-wchar -mno-red-zone"
EFI_CFLAGS="$EFI_CFLAGS -DEFI_FUNCTION_WRAPPER"
EFI_CFLAGS="$EFI_CFLAGS -Wall -Wextra -std=c11"

echo -e "${GREEN}Compiling hello UEFI...${NC}"

# Compile
$CC $EFI_CFLAGS -c "$ROOT_DIR/arch/x86_64/uefi/hello_uefi.c" -o "$BUILD_DIR/hello_uefi.o"
if [ $? -ne 0 ]; then
    echo -e "${RED}Failed to compile hello UEFI${NC}"
    exit 1
fi

echo -e "${GREEN}Linking hello UEFI application...${NC}"

# Link with detailed options
ld -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
   -L /usr/lib /usr/lib/crt0-efi-x86_64.o "$BUILD_DIR/hello_uefi.o" \
   -o "$BUILD_DIR/hello_uefi.so" -lefi -lgnuefi

if [ $? -ne 0 ]; then
    echo -e "${RED}Failed to link hello UEFI application${NC}"
    exit 1
fi

echo -e "${GREEN}Converting to EFI format...${NC}"

# Convert to EFI
$OBJCOPY -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
         -j .rela -j .reloc --target=efi-app-x86_64 \
         "$BUILD_DIR/hello_uefi.so" "$BUILD_DIR/hello_uefi.efi"

if [ $? -ne 0 ]; then
    echo -e "${RED}Failed to convert to EFI format${NC}"
    exit 1
fi

# Check file
if [ -f "$BUILD_DIR/hello_uefi.efi" ] && [ -s "$BUILD_DIR/hello_uefi.efi" ]; then
    echo -e "${GREEN}Hello EFI application created: $(stat -c%s "$BUILD_DIR/hello_uefi.efi") bytes${NC}"
    file "$BUILD_DIR/hello_uefi.efi"
else
    echo -e "${RED}Failed to create hello EFI application${NC}"
    exit 1
fi

echo -e "${GREEN}Creating disk image...${NC}"

# Create simple disk image
DISK_IMAGE="$BUILD_DIR/hello_uefi.img"
dd if=/dev/zero of="$DISK_IMAGE" bs=1M count=64 2>/dev/null
mkfs.fat -F32 -s1 -S512 "$DISK_IMAGE"

# Mount and copy files
MOUNT_POINT=$(mktemp -d)
sudo mount -o loop "$DISK_IMAGE" "$MOUNT_POINT"
sudo mkdir -p "$MOUNT_POINT/EFI/BOOT"
sudo cp "$BUILD_DIR/hello_uefi.efi" "$MOUNT_POINT/EFI/BOOT/BOOTX64.EFI"
sudo umount "$MOUNT_POINT"
rmdir "$MOUNT_POINT"

echo -e "${GREEN}Build complete!${NC}"
echo -e "${GREEN}Files created:${NC}"
echo -e "  EFI application: $BUILD_DIR/hello_uefi.efi"
echo -e "  Disk image: $DISK_IMAGE"

echo -e "${GREEN}To test with QEMU:${NC}"
echo -e "  qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -hda $DISK_IMAGE -m 128M -serial stdio" 