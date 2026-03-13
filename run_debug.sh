#!/bin/bash

# Run QEMU with serial output visible on terminal
# This shows both VGA text mode AND serial debug output

QEMU_BIN=""
if command -v qemu-system-i386 >/dev/null 2>&1; then
    QEMU_BIN="qemu-system-i386"
elif command -v qemu-system-x86_64 >/dev/null 2>&1; then
    QEMU_BIN="qemu-system-x86_64"
else
    echo "Error: QEMU not found"
    exit 1
fi

echo "Starting QEMU with debug output..."
echo "Serial output appears below:"
echo "================================"

# Run with serial output to stdio
$QEMU_BIN \
    -drive format=raw,file=build/boot.img,if=floppy \
    -boot a \
    -serial stdio \
    -display vc
