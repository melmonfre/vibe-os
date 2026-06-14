# Hardware Boot Validation

## Known-Working Configurations

### QEMU (Primary Test Target)

| Component | Configuration | Status |
|-----------|--------------|--------|
| Machine   | `q35` or `pc` (i440fx) | Validated |
| CPU       | `qemu64`, `host`, `max` (1-4 cores) | Validated |
| Memory    | 256 MB - 2 GB | Validated |
| Storage   | AHCI on PIIX4 or ICH9 (`-drive file=...,if=none,format=raw -device ahci,id=ahci -device ide-hd,drive=drive0,bus=ahci.0`) | Validated |
| Storage   | USB mass-storage via UHCI (`-drive file=...,if=none,format=raw -device usb-storage,drive=drive0`) | Validated |
| Video     | QXL, VGA, Bochs VBE (`-vga std`, `-vga virtio`, `-device bochs-display`) | Validated (BGA) |
| Video     | Cirrus (`-vga cirrus`) | Detection only |
| Audio     | AC97 (`-audiodev pa,id=pa -device ac97,audiodev=pa`) | Validated |
| Audio     | HDA (`-audiodev pa,id=pa -device hda-output,audiodev=pa`) | Validated |
| Network   | VirtIO (`-netdev user,id=net0 -device virtio-net,netdev=net0`) | Validated |
| Network   | e1000 (`-netdev user,id=net0 -device e1000,netdev=net0`) | Validated |
| Network   | RTL8139 (`-netdev user,id=net0 -device rtl8139,netdev=net0`) | Validated |
| USB       | UHCI (`-device piix3-usb-uhci`) | Validated |
| USB       | OHCI (`-device pci-ohci`)  | Validated |
| USB       | XHCI (`-device qemu-xhci`)  | Functional (control) |
| Input     | PS/2 keyboard + mouse | Validated |
| Input     | USB HID keyboard + mouse | Validated |

### QEMU Quick Start

```sh
make run                    # default AHCI + VGA + VirtIO
make run USB=1              # enable USB controller
make run AUDIO=ac97         # AC97 audio
make run AUDIO=hda          # HDA audio
make run NET=e1000          # e1000 NIC
make run SMP=2              # 2 CPU cores
```

### QEMU Boot Validation

Run the automated boot chain validation:

```sh
python3 tools/validate-boot-chain.py --image build/boot.img --report /tmp/boot-report.md
```

### Known QEMU Limitations

- XHCI USB 3.0: control transfers work, mass-storage BOT is functional but slower than UHCI/OHCI
- Cirrus VGA: detection-only, no native modeset
- QXL: detection-only (no native DRM backend yet)
- SMP with >2 CPUs: functional but IOAPIC routing may degrade under heavy storage+network+audio load
- RTL8139: RX checksum offload not implemented; works in basic passthrough
- e1000: 82540EM emulation only; 82574L not tested

## Real Hardware Profiles

### x86 Laptop (Intel)

| Component | Model | Status |
|-----------|-------|--------|
| CPU       | Core i5-3320M (Ivy Bridge) | Validated |
| Chipset   | QM77 (Panther Point) | Validated |
| Storage   | AHCI SATA SSD | Validated |
| Video     | Intel HD 4000 (i915) | Validated (native modeset) |
| Audio     | HDA Realtek ALC269 | Validated |
| Network   | Intel 82579LM (e1000) | Validated |
| Network   | Intel Centrino 7260 (Wi-Fi) | Functional (WPA2) |
| USB       | EHCI + XHCI | Validated (keyboard, storage) |
| Input     | PS/2 Synaptics touchpad | Validated |

### x86 Desktop (AMD)

| Component | Model | Status |
|-----------|-------|--------|
| CPU       | AMD FX-8350 (Vishera) | Validated |
| Chipset   | AMD 970 + SB950 | Validated |
| Storage   | AHCI SATA HDD | Validated |
| Video     | Radeon HD 7850 (Pitcairn) | Validated (native modeset) |
| Audio     | HDA Realtek ALC892 | Validated |
| Network   | Realtek RTL8111 (RTL8139) | Validated |
| USB       | OHCI + EHCI | Validated |

### Real Hardware Limitations

- NVIDIA GPU: nouveau driver is probe-only; no scanout/modeset support yet
- Suspend/resume: not implemented
- Hotplug (USB, PCIe): not implemented; all devices must be present at boot
- Audio capture: not validated on real hardware
- Wi-Fi: Intel 7260 only; no broadcom/atheros/realtek wireless support
