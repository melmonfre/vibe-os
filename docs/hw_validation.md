# Hardware Boot Validation

> **Acceptance bar (maintainer decision 2026-10-13): real hardware, not QEMU.**
> Boot, USB, video, audio, network and the POSIX surface are all required to
> work on physical T61/T400-class machines. QEMU matrices below are necessary
> but explicitly NOT sufficient. See COMPLETION_ACTION_PLAN "P0 hardware-first".

## Real-Hardware Boot Chain Audit (2026-10-13)

| Item | Mechanism | QEMU behavior | Real-BIOS risk | Status |
|------|-----------|---------------|----------------|--------|
| A20 confirmation | `boot/stage2.asm` memory aliasing probes | masked: SeaBIOS implements int 15h AX=2402h fallback that made boot pass | **fatal**: both probes were dead code — `xor ax,ax` (sets ZF=1) ran before `je`, so the probes could NEVER report A20 enabled; on BIOSes without 2401h/2402h (common on IBM ThinkPads) the loader halted on `a20_error` even when port-92/KBC had just enabled A20 | **FIXED** — flags latched via `setne` while ZF from `cmp` is still live; proof: loader trace now exits enable_a20 at the first probe check (`2FAVBaQ...`) instead of running every method plus the 2402h fallback (`2FAVBabcdq...`); trace read live from the guest via HMP at 0xa02f |
| A20 enable methods | int 15h AX=2401h → port 92 → 8042 KBC (0xD0/0xD1/0xAE), all bounded with timeouts | fine | fine; KBC path handles ThinkPad controllers | OK |
| LBA disk reads | MBR checks int 13h AH=41h extension signature before AH=42h; VBR/stage2 read via DAP with 3x reset-retry | fine | stage1 still requires EDD (pre-EDD BIOS halts with clear error) — accepted for T61/T400 era; **2026-10-13**: MBR VBR-read and stage1 stage2-read gained bounded 3x retry + media reset (transient CF right after power-on on real spin-up drives / USB bridges is invisible to QEMU); kernel ATA path audited: already has bounded retries, soft-reset, per-attempt device re-select and tick-deadline waits | HARDENED |
| Memory map | E820 (ECX=24) → E801 → int 15h AH=88h fallback chain | fine | fine; legacy-BIOS quirks covered by fallback chain | OK |
| Boot stack vs kernel image | kernel loads at 0x10000 (403 KB today), menu stack at 0x70000 used only BEFORE kernel load; kernel handoff stack 0x800000 with linker ASSERT | fine | verified: no region overlap at any phase (0x10000+0x62AB0 = 0x72AB0 is reached only after the menu is done) | OK (re-checked after kernel growth) |

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
| Network   | e1000 (`-netdev user,id=net0 -device e1000,netdev=net0`) | NOT validated - driver exists but has zero callers; never wired into the live network stack (COMPLETION_ACTION_PLAN P3) |
| Network   | RTL8139 (`-netdev user,id=net0 -device rtl8139,netdev=net0`) | NOT validated - driver exists but has zero callers; never wired into the live network stack (COMPLETION_ACTION_PLAN P3) |
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
- SMP: AP bring-up (INIT/SIPI) works, but the per-CPU scheduler is gated behind BOOTINFO_FLAG_EXPERIMENTAL_SMP (stage2 key M) and has an open SMP memory-corruption issue (COMPLETION_ACTION_PLAN P4); default boot is BSP-only
- RTL8139 / e1000: drivers are dead code today (no init call sites, no stack attachment); the only live datapath is the private virtio-net in kernel/microkernel/network.c

## Real Hardware Profiles

> STATUS EVIDENCE (2026-10-12 audit): the rows below are UNVERIFIED in this repository.
> No serial logs, validation reports or filled matrix rows back them, and README.md
> still lists real-hardware audio/video/SMP/network as open. Treat every
> "Validated" here as a claim that needs re-proof: attach the boot serial log /
> validate-* report per row (AUDIO_HARDWARE_VALIDATION_MATRIX.md pattern)
> before relying on it. Wi-Fi is worse than unverified: scan/connect are UI
> placeholders with hardcoded SSIDs and no 802.11 data plane at all.

### x86 Laptop (Intel)

| Component | Model | Status |
|-----------|-------|--------|
| CPU       | Core i5-3320M (Ivy Bridge) | Validated |
| Chipset   | QM77 (Panther Point) | Validated |
| Storage   | AHCI SATA SSD | Validated |
| Video     | Intel HD 4000 (i915) | Validated (native modeset) |
| Audio     | HDA Realtek ALC269 | Validated |
| Network   | Intel 82579LM (e1000) | Validated |
| Network   | Intel Centrino 7260 (Wi-Fi) | NOT functional - scan list is hardcoded and "connect" fabricates an IP; no 802.11 auth/assoc/CCMP path exists (COMPLETION_ACTION_PLAN P3) |
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
- Wi-Fi: no real wireless support at all (placeholder UI only); see COMPLETION_ACTION_PLAN P3
